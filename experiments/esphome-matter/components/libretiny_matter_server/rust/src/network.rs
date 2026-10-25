//! ESPHome owns Wi-Fi configuration. Matter commissions on the existing LAN.
use rs_matter::dm::clusters::gen_diag::{InterfaceTypeEnum, NetifDiag, NetifInfo};
use rs_matter::dm::clusters::net_comm::{
    NetCtl, NetCtlError, NetCtlStatus, NetworkCommissioningStatusEnum, NetworkScanInfo,
    NetworkType, Networks, NetworksError, WirelessCreds,
};
use rs_matter::dm::clusters::wifi_diag::{WifiDiag, WirelessDiag};
use rs_matter::error::{Error, ErrorCode};
use rs_matter::utils::sync::DynBase;

use crate::platform::{ltm_platform_connected, Config};

#[derive(Clone, Copy)]
pub struct EspHomeWifi(pub &'static Config);
impl EspHomeWifi {
    fn ssid(&self) -> &[u8] {
        &self.0.ssid[..self.0.ssid_len as usize]
    }
}
impl DynBase for EspHomeWifi {}
impl WirelessDiag for EspHomeWifi {
    fn connected(&self) -> Result<bool, Error> {
        Ok(unsafe { ltm_platform_connected() })
    }
}
impl WifiDiag for EspHomeWifi {}
impl NetifDiag for EspHomeWifi {
    fn netifs(&self, f: &mut dyn FnMut(&NetifInfo) -> Result<(), Error>) -> Result<(), Error> {
        f(&NetifInfo {
            name: "wlan",
            operational: self.connected()?,
            offprem_svc_reachable_ipv4: None,
            offprem_svc_reachable_ipv6: None,
            hw_addr: &self.0.mac,
            ipv4_addrs: &[self.0.ipv4.into()],
            ipv6_addrs: &[self.0.ipv6.into()],
            netif_type: InterfaceTypeEnum::WiFi,
            netif_index: self.0.interface,
        })
    }
}
impl NetCtl for EspHomeWifi {
    fn net_type(&self) -> NetworkType {
        NetworkType::Wifi
    }
    async fn scan<F>(&self, _: Option<&[u8]>, _: F) -> Result<(), NetCtlError>
    where
        F: FnMut(&NetworkScanInfo) -> Result<(), Error>,
    {
        Err(NetCtlError::Other(ErrorCode::InvalidAction.into()))
    }
    async fn connect(&self, _: &WirelessCreds<'_>) -> Result<(), NetCtlError> {
        // Never acknowledge a Wi-Fi credential change that ESPHome did not apply.
        Err(NetCtlError::Other(ErrorCode::InvalidAction.into()))
    }
}
impl NetCtlStatus for EspHomeWifi {
    fn last_networking_status(&self) -> Result<Option<NetworkCommissioningStatusEnum>, Error> {
        Ok(self
            .connected()?
            .then_some(NetworkCommissioningStatusEnum::Success))
    }
    fn last_network_id<F, R>(&self, f: F) -> Result<R, Error>
    where
        F: FnOnce(Option<&[u8]>) -> Result<R, Error>,
    {
        f(Some(self.ssid()))
    }
    fn last_connect_error_value(&self) -> Result<Option<i32>, Error> {
        Ok(None)
    }
}
impl Networks for EspHomeWifi {
    fn max_networks(&self) -> Result<u8, Error> {
        Ok(1)
    }
    fn networks(&self, f: &mut dyn FnMut(&[u8]) -> Result<(), Error>) -> Result<(), Error> {
        f(self.ssid())
    }
    fn creds(
        &self,
        _: &[u8],
        _: &mut dyn FnMut(&WirelessCreds) -> Result<(), Error>,
    ) -> Result<u8, NetworksError> {
        Err(NetworksError::Other(ErrorCode::InvalidAction.into()))
    }
    fn next_creds(
        &self,
        _: Option<&[u8]>,
        _: &mut dyn FnMut(&WirelessCreds) -> Result<(), Error>,
    ) -> Result<bool, Error> {
        Ok(false)
    }
    fn enabled(&self) -> Result<bool, Error> {
        self.connected()
    }
    fn set_enabled(&mut self, enabled: bool) -> Result<(), Error> {
        if enabled {
            Ok(())
        } else {
            Err(ErrorCode::InvalidAction.into())
        }
    }
    fn add_or_update(&mut self, _: &WirelessCreds<'_>) -> Result<u8, NetworksError> {
        Err(NetworksError::Other(ErrorCode::InvalidAction.into()))
    }
    fn reorder(&mut self, index: u8, network: &[u8]) -> Result<u8, NetworksError> {
        if index == 0 && network == self.ssid() {
            Ok(0)
        } else {
            Err(NetworksError::Other(ErrorCode::InvalidAction.into()))
        }
    }
    fn remove(&mut self, _: &[u8]) -> Result<u8, NetworksError> {
        Err(NetworksError::Other(ErrorCode::InvalidAction.into()))
    }
    fn managed(&self) -> Result<bool, Error> {
        Ok(true)
    }
    fn set_managed(&mut self, _: bool) -> Result<(), Error> {
        Ok(())
    }
    // Fabric reset must not erase ESPHome's own Wi-Fi configuration.
    fn reset(&mut self) -> Result<(), Error> {
        Ok(())
    }
    fn load(&mut self, _: &[u8]) -> Result<(), Error> {
        Ok(())
    }
    fn save(&self, _: &mut [u8]) -> Result<Option<usize>, Error> {
        Ok(None)
    }
}
