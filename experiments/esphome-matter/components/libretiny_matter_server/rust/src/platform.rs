//! Narrow C ABI: LibreTiny owns the network, entropy, clock and persistent storage.
use core::alloc::{GlobalAlloc, Layout};
use core::fmt::{self, Write};
use core::net::{IpAddr, Ipv4Addr, Ipv6Addr, SocketAddr, SocketAddrV6};

use embassy_time::Timer;
use rs_matter::crypto::{TryCryptoRng, TryRng};
use rs_matter::error::{Error, ErrorCode};
use rs_matter::persist::KvBlobStore;
use rs_matter::transport::network::{Address, NetworkMulticast, NetworkReceive, NetworkSend};

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Config {
    pub passcode: u32,
    pub discriminator: u16,
    pub commissioning_seconds: u16,
    pub ipv4: [u8; 4],
    pub ipv6: [u8; 16],
    pub interface: u32,
    pub hostname: [u8; 16],
    pub ssid: [u8; 32],
    pub ssid_len: u8,
    pub mac: [u8; 8],
}

#[repr(C)]
#[derive(Default)]
pub struct Peer {
    pub ip: [u8; 16],
    pub scope: u32,
    pub port: u16,
    pub ipv4: u8,
}

// Keep the C and Rust wire layout identical on ARMv5TE and the host fixture.
const _: () = assert!(core::mem::size_of::<Config>() == 92);
const _: () = assert!(core::mem::size_of::<Peer>() == 24);

unsafe extern "C" {
    fn ltm_platform_now_ms() -> u64;
    fn ltm_platform_random(data: *mut u8, len: usize) -> i32;
    fn ltm_platform_send(socket: u8, data: *const u8, len: usize, peer: *const Peer) -> i32;
    fn ltm_platform_recv(socket: u8, data: *mut u8, len: usize, peer: *mut Peer) -> i32;
    fn ltm_platform_available(socket: u8) -> i32;
    fn ltm_platform_multicast(socket: u8, ip: *const u8, ipv4: bool, join: bool) -> i32;
    fn ltm_platform_load(key: u16, data: *mut u8, capacity: usize) -> i32;
    fn ltm_platform_store(key: u16, data: *const u8, len: usize) -> i32;
    fn ltm_platform_remove(key: u16) -> i32;
    fn ltm_platform_log(level: u8, data: *const u8, len: usize);
    pub fn ltm_platform_switch(on: bool);
    pub fn ltm_platform_connected() -> bool;
    fn ltm_platform_alloc(size: usize, align: usize) -> *mut u8;
    fn ltm_platform_free(ptr: *mut u8);
    fn ltm_platform_enter_critical();
    fn ltm_platform_exit_critical();
    pub fn ltm_platform_fatal() -> !;
}

struct Allocator;
#[global_allocator]
static ALLOCATOR: Allocator = Allocator;

// SAFETY: the platform allocates suitably aligned memory and accepts exactly
// those pointers in free. It never calls back into Rust during allocation.
unsafe impl GlobalAlloc for Allocator {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        ltm_platform_alloc(layout.size(), layout.align())
    }
    unsafe fn dealloc(&self, ptr: *mut u8, _: Layout) {
        ltm_platform_free(ptr)
    }
}

struct CriticalSection;
critical_section::set_impl!(CriticalSection);
// SAFETY: the C bridge provides nesting-aware exclusion on this single-core MCU.
unsafe impl critical_section::Impl for CriticalSection {
    unsafe fn acquire() -> critical_section::RawRestoreState {
        ltm_platform_enter_critical();
    }
    unsafe fn release(_: critical_section::RawRestoreState) {
        ltm_platform_exit_critical();
    }
}

struct Clock;
embassy_time_driver::time_driver_impl!(static CLOCK: Clock = Clock);
impl embassy_time_driver::Driver for Clock {
    fn now(&self) -> u64 {
        unsafe { ltm_platform_now_ms() }
    }
    fn schedule_wake(&self, _: u64, _: &core::task::Waker) {
        // There is one root future, polled unconditionally by the platform every
        // 10 ms. This fixed polling executor needs no interrupt/timer allocation.
    }
}

#[derive(Clone, Copy)]
pub struct HardwareRng;
impl TryRng for HardwareRng {
    type Error = core::convert::Infallible;
    fn try_next_u32(&mut self) -> Result<u32, Self::Error> {
        let mut bytes = [0; 4];
        self.try_fill_bytes(&mut bytes)?;
        Ok(u32::from_le_bytes(bytes))
    }
    fn try_next_u64(&mut self) -> Result<u64, Self::Error> {
        let mut bytes = [0; 8];
        self.try_fill_bytes(&mut bytes)?;
        Ok(u64::from_le_bytes(bytes))
    }
    fn try_fill_bytes(&mut self, bytes: &mut [u8]) -> Result<(), Self::Error> {
        if unsafe { ltm_platform_random(bytes.as_mut_ptr(), bytes.len()) } != 0 {
            log::error!("Hardware entropy failed; stopping Matter");
            unsafe { ltm_platform_fatal() }
        }
        Ok(())
    }
}
impl TryCryptoRng for HardwareRng {}

#[derive(Clone, Copy)]
pub struct Socket(pub u8);
impl NetworkSend for Socket {
    async fn send_to(&mut self, data: &[u8], addr: Address) -> Result<(), Error> {
        let addr = addr.udp().ok_or(ErrorCode::NoNetworkInterface)?;
        let mut peer = Peer {
            port: addr.port(),
            ..Peer::default()
        };
        match addr {
            SocketAddr::V4(addr) => {
                peer.ip[..4].copy_from_slice(&addr.ip().octets());
                peer.ipv4 = 1;
            }
            SocketAddr::V6(addr) => {
                peer.ip = addr.ip().octets();
                peer.scope = addr.scope_id();
            }
        }
        for _ in 0..100 {
            let result = unsafe { ltm_platform_send(self.0, data.as_ptr(), data.len(), &peer) };
            if result == data.len() as i32 {
                return Ok(());
            }
            if result != -11 {
                return Err(ErrorCode::NoNetworkInterface.into());
            }
            Timer::after_millis(10).await;
        }
        Err(ErrorCode::NoNetworkInterface.into())
    }
}
impl NetworkReceive for Socket {
    async fn wait_available(&mut self) -> Result<(), Error> {
        loop {
            match unsafe { ltm_platform_available(self.0) } {
                1 => return Ok(()),
                0 => Timer::after_millis(10).await,
                _ => return Err(ErrorCode::NoNetworkInterface.into()),
            }
        }
    }
    async fn recv_from(&mut self, buffer: &mut [u8]) -> Result<(usize, Address), Error> {
        // Bound work per poll even when a peer continuously floods the socket.
        Timer::after_millis(1).await;
        loop {
            let mut peer = Peer::default();
            let result =
                unsafe { ltm_platform_recv(self.0, buffer.as_mut_ptr(), buffer.len(), &mut peer) };
            if result == -11 {
                Timer::after_millis(10).await;
                continue;
            }
            if result < 0 || result as usize > buffer.len() {
                return Err(ErrorCode::NoNetworkInterface.into());
            }
            let addr = if peer.ipv4 != 0 {
                SocketAddr::new(
                    IpAddr::V4(Ipv4Addr::new(
                        peer.ip[0], peer.ip[1], peer.ip[2], peer.ip[3],
                    )),
                    peer.port,
                )
            } else {
                SocketAddr::V6(SocketAddrV6::new(
                    Ipv6Addr::from(peer.ip),
                    peer.port,
                    0,
                    peer.scope,
                ))
            };
            return Ok((result as usize, Address::Udp(addr)));
        }
    }
}
impl NetworkMulticast for Socket {
    async fn join(&mut self, addr: IpAddr) -> Result<(), Error> {
        self.multicast(addr, true)
    }
    async fn leave(&mut self, addr: IpAddr) -> Result<(), Error> {
        self.multicast(addr, false)
    }
}
impl Socket {
    fn multicast(self, addr: IpAddr, join: bool) -> Result<(), Error> {
        let result = match addr {
            IpAddr::V4(ip) => unsafe {
                ltm_platform_multicast(self.0, ip.octets().as_ptr(), true, join)
            },
            IpAddr::V6(ip) => unsafe {
                ltm_platform_multicast(self.0, ip.octets().as_ptr(), false, join)
            },
        };
        if result == 0 {
            Ok(())
        } else {
            Err(ErrorCode::NoNetworkInterface.into())
        }
    }
}

pub struct Storage;
impl KvBlobStore for Storage {
    fn load<'a>(&mut self, key: u16, buf: &'a mut [u8]) -> Result<Option<&'a [u8]>, Error> {
        let result = unsafe { ltm_platform_load(key, buf.as_mut_ptr(), buf.len()) };
        if result == -2 {
            return Ok(None);
        }
        if result < 0 || result as usize > buf.len() {
            return Err(ErrorCode::StdIoError.into());
        }
        Ok(Some(&buf[..result as usize]))
    }
    fn store(&mut self, key: u16, data: &[u8], _: &mut [u8]) -> Result<(), Error> {
        if unsafe { ltm_platform_store(key, data.as_ptr(), data.len()) } == 0 {
            Ok(())
        } else {
            Err(ErrorCode::StdIoError.into())
        }
    }
    fn remove(&mut self, key: u16, _: &mut [u8]) -> Result<(), Error> {
        match unsafe { ltm_platform_remove(key) } {
            0 | -2 => Ok(()),
            _ => Err(ErrorCode::StdIoError.into()),
        }
    }
}

struct LogBuffer {
    bytes: [u8; 256],
    len: usize,
}
impl Write for LogBuffer {
    fn write_str(&mut self, text: &str) -> fmt::Result {
        let count = text.len().min(self.bytes.len() - self.len);
        self.bytes[self.len..self.len + count].copy_from_slice(&text.as_bytes()[..count]);
        self.len += count;
        Ok(())
    }
}
struct Logger;
static LOGGER: Logger = Logger;
impl log::Log for Logger {
    fn enabled(&self, metadata: &log::Metadata) -> bool {
        metadata.level() <= log::Level::Info
    }
    fn log(&self, record: &log::Record) {
        if !self.enabled(record.metadata()) {
            return;
        }
        let mut buffer = LogBuffer {
            bytes: [0; 256],
            len: 0,
        };
        let _ = write!(buffer, "{}: {}", record.target(), record.args());
        unsafe { ltm_platform_log(record.level() as u8, buffer.bytes.as_ptr(), buffer.len) };
    }
    fn flush(&self) {}
}
pub fn init_logging() {
    // Only the single platform-owned Matter task initializes and uses this runtime.
    unsafe {
        let _ = log::set_logger_racy(&LOGGER);
    }
    unsafe {
        log::set_max_level_racy(log::LevelFilter::Info);
    }
}
