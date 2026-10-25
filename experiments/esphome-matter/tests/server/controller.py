"""Interoperability tests using the independent ConnectedHomeIP controller SDK."""

import argparse
import asyncio
import json
import random
import socket
from pathlib import Path

import chip.CertificateAuthority
import chip.clusters as clusters
import chip.exceptions
import chip.native
from chip.ChipDeviceCtrl import DiscoveryFilterType
from chip.ChipStack import ChipStack
from chip.interaction_model import InteractionModelError, Status

NODE = 0x12344321


async def exercise(controller, admin, commission, discover, wrong_pin, fail_storage):
    if wrong_pin:
        # The SDK can emit multiple completion callbacks for one failed PASE.
        # Keep that controller separate from the subsequent successful pairing.
        rejected_controller = admin.NewController(nodeId=112235)
        try:
            try:
                await asyncio.wait_for(
                    rejected_controller.CommissionIP("::1", 20202022, NODE), 45
                )
            except chip.exceptions.ChipStackError as error:
                assert error.err in {3, 0xAC}, error
                print("PASS wrong passcode rejected", flush=True)
            else:
                raise AssertionError("wrong passcode was accepted")
        finally:
            rejected_controller.Shutdown()
    if commission:
        if discover:
            pairing = controller.CommissionOnNetwork(
                NODE, 20202021, DiscoveryFilterType.LONG_DISCRIMINATOR, 3840
            )
        else:
            pairing = controller.CommissionIP("::1", 20202021, NODE)
        try:
            await asyncio.wait_for(pairing, 90)
        except chip.exceptions.ChipStackError:
            if not fail_storage:
                raise
            print(
                "PASS commissioning rejected after fabric storage failure", flush=True
            )
            return {"storage_failure_rejected": True}
        assert not fail_storage, "commissioning succeeded despite failed fabric storage"
        print(
            "PASS commissioning: PASE, attestation, CSR, AddNOC, CASE, complete",
            flush=True,
        )

    attribute = clusters.OnOff.Attributes.OnOff
    label = clusters.BasicInformation.Attributes.NodeLabel

    async def read_on_off():
        data = await controller.ReadAttribute(NODE, [(1, attribute)])
        return data[1][clusters.OnOff][attribute]

    if not commission:
        assert await read_on_off() is True
        restored = await controller.ReadAttribute(NODE, [(0, label)])
        assert (
            restored[0][clusters.BasicInformation][label]
            == "LibreTiny interoperability"
        )
        print(
            "PASS reboot: stored fabric, CASE authentication and OnOff state restored",
            flush=True,
        )

    for command, expected in [
        (clusters.OnOff.Commands.Off(), False),
        (clusters.OnOff.Commands.On(), True),
        (clusters.OnOff.Commands.Toggle(), False),
        (clusters.OnOff.Commands.Toggle(), True),
    ]:
        await controller.SendCommand(NODE, 1, command)
        assert await read_on_off() is expected
        print(f"PASS command {type(command).__name__}: {expected}", flush=True)

    updates = []
    subscription = await controller.ReadAttribute(
        NODE, [(1, attribute)], reportInterval=(0, 10), autoResubscribe=False
    )
    subscription.SetAttributeUpdateCallback(
        lambda path, transaction: updates.append(transaction.GetAttribute(path))
    )
    await controller.SendCommand(NODE, 1, clusters.OnOff.Commands.Off())
    for _ in range(100):
        if False in updates:
            break
        await asyncio.sleep(0.1)
    assert False in updates, f"missing subscription report: {updates}"
    subscription.Shutdown()
    print("PASS subscription: changed OnOff attribute reported", flush=True)

    basic = await controller.ReadAttribute(
        NODE, [(0, clusters.BasicInformation.Attributes.ProductName)]
    )
    assert (
        basic[0][clusters.BasicInformation][
            clusters.BasicInformation.Attributes.ProductName
        ]
        == "LibreTiny test light"
    )
    print("PASS BasicInformation product identity", flush=True)

    written = await controller.WriteAttribute(
        NODE, [(0, label("LibreTiny interoperability"))]
    )
    assert len(written) == 1 and written[0].Status == Status.Success, written
    observed = await controller.ReadAttribute(NODE, [(0, label)])
    assert observed[0][clusters.BasicInformation][label] == "LibreTiny interoperability"
    print("PASS WriteAttribute: NodeLabel updated and read back", flush=True)
    rejected = await controller.WriteAttribute(NODE, [(1, attribute(False))])
    assert (
        len(rejected) == 1 and rejected[0].Status == Status.UnsupportedWrite
    ), rejected
    print("PASS WriteAttribute: read-only OnOff attribute rejected", flush=True)

    outsider = admin.NewController(nodeId=112234)
    try:
        try:
            await outsider.SendCommand(NODE, 1, clusters.OnOff.Commands.Off())
        except InteractionModelError as error:
            assert error.status == Status.UnsupportedAccess, error
        else:
            raise AssertionError("controller without an ACL grant changed the endpoint")
        print("PASS ACL: authenticated controller without grant rejected", flush=True)
    finally:
        outsider.Shutdown()

    try:
        await controller.SendCommand(NODE, 99, clusters.OnOff.Commands.On())
    except InteractionModelError as error:
        assert error.status == Status.UnsupportedEndpoint, error
    else:
        raise AssertionError("unsupported endpoint accepted command")
    print("PASS unsupported endpoint rejected", flush=True)

    rng = random.Random(7231)
    with socket.socket(socket.AF_INET6, socket.SOCK_DGRAM) as udp:
        for length in [*range(96), 1584, 4096]:
            udp.sendto(rng.randbytes(length), ("::1", 5540))
    await asyncio.sleep(2)
    assert isinstance(await read_on_off(), bool)
    print(
        "PASS malformed UDP: 98 datagrams; authenticated read still works", flush=True
    )

    # Keep the final state through the upstream handler's deferred persistence.
    await controller.SendCommand(NODE, 1, clusters.OnOff.Commands.On())
    await asyncio.sleep(4)
    print("PASS persisted-state checkpoint: OnOff=true", flush=True)
    return {
        "commissioned": commission,
        "commands": 4,
        "subscription": True,
        "basic_info": True,
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--storage", required=True, type=Path)
    parser.add_argument("--paa", required=True)
    parser.add_argument("--existing", action="store_true")
    parser.add_argument("--discover", action="store_true")
    parser.add_argument("--wrong-pin", action="store_true")
    parser.add_argument("--expect-storage-failure", action="store_true")
    args = parser.parse_args()
    chip.native.Init()
    stack = ChipStack(str(args.storage), enableServerInteractions=False)
    manager = chip.CertificateAuthority.CertificateAuthorityManager(stack)
    manager.LoadAuthoritiesFromStorage()
    authority = (
        manager.activeCaList[0]
        if manager.activeCaList
        else manager.NewCertificateAuthority()
    )
    admin = (
        authority.adminList[0]
        if authority.adminList
        else authority.NewFabricAdmin(vendorId=0xFFF1, fabricId=1)
    )
    controller = admin.NewController(nodeId=112233, paaTrustStorePath=args.paa)
    try:
        result = asyncio.run(
            exercise(
                controller,
                admin,
                not args.existing,
                args.discover,
                args.wrong_pin,
                args.expect_storage_failure,
            )
        )
        print(json.dumps(result), flush=True)
    finally:
        controller.Shutdown()
        manager.Shutdown()
        stack.Shutdown()


if __name__ == "__main__":
    main()
