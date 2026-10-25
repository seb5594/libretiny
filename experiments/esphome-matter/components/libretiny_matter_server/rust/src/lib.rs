//! Native Matter server. All protocol, crypto and cluster handling comes from
//! the pinned Project CHIP rs-matter implementation.
#![no_std]
#![feature(type_alias_impl_trait)]
#![recursion_limit = "256"]

use core::future::Future;
use core::mem::MaybeUninit;
use core::pin::{pin, Pin};
use core::task::{Context, Poll, Waker};

use embassy_futures::select::select4;
use rs_matter::crypto::default_crypto;
use rs_matter::dm::clusters::app::on_off::{self, NoLevelControl, OnOffHandler, OnOffHooks};
use rs_matter::dm::clusters::basic_info::BasicInfoConfig;
use rs_matter::dm::clusters::desc::{self, ClusterHandler as _};
use rs_matter::dm::clusters::groups::{self, ClusterHandler as _};
use rs_matter::dm::clusters::identify::{self, IdentifyHandler};
use rs_matter::dm::devices::test::{DAC_PRIVKEY, TEST_DEV_ATT, TEST_DEV_DET};
use rs_matter::dm::devices::DEV_TYPE_ON_OFF_LIGHT;
use rs_matter::dm::endpoints;
use rs_matter::dm::{Async, Cluster, DataModel, Dataver, Endpoint, Node};
use rs_matter::error::Error;
use rs_matter::im::{InteractionModel, InteractionModelState};
use rs_matter::respond::DefaultResponder;
use rs_matter::sc::pase::{Spake2pVerifierPassword, Spake2pVerifierPasswordRef};
use rs_matter::transport::exchange::MatterBuffers;
use rs_matter::transport::network::mdns::builtin::{BuiltinMdns, Host};
use rs_matter::transport::network::{Ipv4Addr, Ipv6Addr};
use rs_matter::utils::init::{init, Init, InitMaybeUninit};
use rs_matter::utils::select::Coalesce;
use rs_matter::{clusters, devices, root_endpoint, BasicCommData, Matter, MATTER_PORT};

mod network;
mod platform;
use network::EspHomeWifi;
use platform::{Config, HardwareRng, Socket, Storage};

struct VirtualLight;
impl OnOffHooks for VirtualLight {
    const CLUSTER: Cluster<'static> = on_off::test::TestOnOffDeviceLogic::CLUSTER;
    fn set_on_off(&self, on: bool) {
        log::info!("Endpoint 1: OnOff={}", on);
        unsafe { platform::ltm_platform_switch(on) };
    }
    async fn handle_off_with_effect(&self, _: on_off::EffectVariantEnum) {}
}

struct Stack {
    matter: Matter<'static>,
    buffers: MatterBuffers,
    state: InteractionModelState<EspHomeWifi>,
}
impl Stack {
    fn init(info: &'static BasicInfoConfig<'static>, config: &'static Config) -> impl Init<Self> {
        let comm = BasicCommData {
            password: Spake2pVerifierPassword::new_from_ref(Spake2pVerifierPasswordRef::new(
                &config.passcode.to_le_bytes(),
            )),
            discriminator: config.discriminator,
        };
        init!(Self {
            matter <- Matter::init(info, comm, &TEST_DEV_ATT, MATTER_PORT),
            buffers <- MatterBuffers::init(),
            state <- InteractionModelState::init(EspHomeWifi(config)),
        })
    }
}

const NODE: Node<'static> = Node {
    endpoints: &[
        root_endpoint!(wifi),
        Endpoint::new(
            1,
            devices!(DEV_TYPE_ON_OFF_LIGHT),
            clusters!(
                desc::DescHandler::CLUSTER,
                identify::CLUSTER,
                groups::GroupsHandler::CLUSTER,
                VirtualLight::CLUSTER
            ),
        ),
    ],
};

fn data_model<'a>(
    light: &'a OnOffHandler<'a, VirtualLight, NoLevelControl>,
    identify: &'a IdentifyHandler,
    wifi: &'a EspHomeWifi,
) -> impl DataModel + 'a {
    let mut random = HardwareRng;
    (
        NODE,
        endpoints::WifiSysHandlerBuilder::new(wifi, wifi)
            .netif_diag(wifi)
            .build(random)
            .chain(
                |e, c| e == 1 && c == desc::DescHandler::CLUSTER.id,
                Async(desc::DescHandler::new(Dataver::new_rand(&mut random)).adapt()),
            )
            .chain(
                |e, c| e == 1 && c == identify::CLUSTER.id,
                Async(identify::HandlerAdaptor(identify)),
            )
            .chain(
                |e, c| e == 1 && c == groups::GroupsHandler::CLUSTER.id,
                Async(
                    groups::GroupsHandler::new_with_identify(
                        Dataver::new_rand(&mut random),
                        identify,
                    )
                    .adapt(),
                ),
            )
            .chain(
                |e, c| e == 1 && c == VirtualLight::CLUSTER.id,
                Async(on_off::HandlerAdaptor(light)),
            ),
    )
}

type ServiceFuture = impl Future<Output = Result<(), Error>>;
#[define_opaque(ServiceFuture)]
fn service(stack: &'static Stack, config: &'static Config) -> ServiceFuture {
    async move {
        let matter = &stack.matter;
        let kv = matter.kv(Storage);
        log::info!("Loading fabric credentials and Matter state");
        matter.startup(&kv)?;
        let crypto = default_crypto(HardwareRng, DAC_PRIVKEY);
        let light = OnOffHandler::new_standalone(
            Dataver::new_rand(&mut HardwareRng),
            1,
            rs_matter::persist::VENDOR_KEYS_START + 0x10,
            VirtualLight,
        );
        let identify = IdentifyHandler::new(Dataver::new_rand(&mut HardwareRng));
        let wifi = EspHomeWifi(config);
        let im = InteractionModel::new(
            matter,
            &crypto,
            &stack.buffers,
            data_model(&light, &identify, &wifi),
            &kv,
            &stack.state,
        );
        im.startup().await?;
        log::info!(
            "Matter endpoint ready; commissioned={}",
            matter.has_fabrics()
        );
        if !matter.has_fabrics() {
            matter.open_basic_comm_window(config.commissioning_seconds, &crypto, &())?;
            log::info!(
                "On-network commissioning open for {} seconds",
                config.commissioning_seconds
            );
        }
        let hostname_len = config
            .hostname
            .iter()
            .position(|b| *b == 0)
            .unwrap_or(config.hostname.len());
        let hostname = core::str::from_utf8(&config.hostname[..hostname_len]).unwrap();
        let ipv6 = [Ipv6Addr::from(config.ipv6)];
        let host = Host {
            hostname,
            ip: Ipv4Addr::from(config.ipv4),
            ipv6: &ipv6,
        };
        let responder = DefaultResponder::new(&im);
        let mut mdns = BuiltinMdns::new();
        log::info!("Starting UDP/5540, mDNS, secure sessions and subscriptions");
        let result = select4(
            pin!(matter.run(&crypto, Socket(0), Socket(0), Socket(0))),
            pin!(mdns.run(
                Socket(1),
                Socket(1),
                &host,
                Some(host.ip),
                (config.interface != 0).then_some(config.interface),
                matter,
                &crypto
            )),
            pin!(responder.run::<4, 4>()),
            pin!(im.run()),
        )
        .coalesce()
        .await;
        result
    }
}

// SAFETY: these slots are private to one platform task. init is called once;
// no pointer to the future escapes and it is never moved after its first poll.
static mut CONFIG: MaybeUninit<Config> = MaybeUninit::uninit();
static mut INFO: MaybeUninit<BasicInfoConfig<'static>> = MaybeUninit::uninit();
static mut STACK: MaybeUninit<Stack> = MaybeUninit::uninit();
static mut FUTURE: MaybeUninit<ServiceFuture> = MaybeUninit::uninit();
static mut INITIALIZED: bool = false;
static mut FINISHED: bool = false;

#[no_mangle]
pub unsafe extern "C" fn ltm_server_init(config: *const Config) -> i32 {
    if config.is_null() || INITIALIZED {
        return -1;
    }
    let cfg = &*config;
    let forbidden = [
        11111111, 22222222, 33333333, 44444444, 55555555, 66666666, 77777777, 88888888, 99999999,
        12345678, 87654321,
    ];
    if cfg.discriminator > 4095
        || !(180..=900).contains(&cfg.commissioning_seconds)
        || cfg.passcode == 0
        || cfg.passcode > 99999998
        || cfg.hostname[0] == 0
        || forbidden.contains(&cfg.passcode)
        || cfg.ssid_len > 32
        || core::str::from_utf8(&cfg.hostname).is_err()
    {
        return -1;
    }
    platform::init_logging();
    let cfg = (&mut *core::ptr::addr_of_mut!(CONFIG)).write(*cfg);
    let hostname_len = cfg
        .hostname
        .iter()
        .position(|b| *b == 0)
        .unwrap_or(cfg.hostname.len());
    let serial = core::str::from_utf8(&cfg.hostname[..hostname_len]).unwrap();
    let info = (&mut *core::ptr::addr_of_mut!(INFO)).write(BasicInfoConfig {
        serial_no: serial,
        device_name: "LibreTiny Matter",
        product_name: "LibreTiny test light",
        // Slow ARM9 crypto must not cause aggressive retransmission bursts.
        sai: Some(1000),
        sii: Some(1000),
        ..TEST_DEV_DET
    });
    let stack = (&mut *core::ptr::addr_of_mut!(STACK)).init_with(Stack::init(info, cfg));
    (&mut *core::ptr::addr_of_mut!(FUTURE)).write(service(stack, cfg));
    INITIALIZED = true;
    log::info!(
        "Matter memory: state={} bytes, future={} bytes",
        core::mem::size_of::<Stack>(),
        core::mem::size_of::<ServiceFuture>()
    );
    0
}

#[no_mangle]
pub unsafe extern "C" fn ltm_server_poll() -> i32 {
    if !INITIALIZED || FINISHED {
        return -1;
    }
    let future = (&mut *core::ptr::addr_of_mut!(FUTURE)).assume_init_mut();
    match Pin::new_unchecked(future).poll(&mut Context::from_waker(Waker::noop())) {
        Poll::Pending => 0,
        Poll::Ready(result) => {
            FINISHED = true;
            log::error!("Matter server stopped: {:?}", result);
            -1
        }
    }
}

/// Stop on the same task that owns init/poll, before changing network addresses.
#[no_mangle]
pub unsafe extern "C" fn ltm_server_stop() {
    if INITIALIZED {
        // Drop all borrowers before the underlying protocol state. Wakers are
        // static no-ops and no callbacks retain a reference to either slot.
        (&mut *core::ptr::addr_of_mut!(FUTURE)).assume_init_drop();
        (&mut *core::ptr::addr_of_mut!(STACK)).assume_init_drop();
        INITIALIZED = false;
        FINISHED = false;
        log::info!("Matter stopped; committed fabrics retained in storage");
    }
}

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    log::error!("Matter runtime panic; stopping protocol task");
    unsafe { platform::ltm_platform_fatal() }
}
