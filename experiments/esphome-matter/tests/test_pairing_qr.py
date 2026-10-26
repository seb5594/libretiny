"""Decode generated QR images independently and reject ambiguous device logs."""

from pathlib import Path

import pytest
import zxingcpp
from PIL import Image
from tools.render_matter_qr import read_setup_log, render

PAYLOAD = "MT:-24J0AFN00KA0648G00"
MANUAL = "34970112332"


def test_qr_image_roundtrip(tmp_path):
    png, svg, page = render(PAYLOAD, tmp_path / "matter", MANUAL)
    with Image.open(png) as image:
        decoded = zxingcpp.read_barcode(image)
        assert decoded is not None
        assert decoded.text == PAYLOAD
        # Four-module white quiet zone around the QR symbol.
        assert image.convert("RGB").getpixel((0, 0)) == (255, 255, 255)
    assert "<svg" in svg.read_text()
    assert (
        svg.read_bytes()
        == (
            Path(__file__).resolve().parents[1] / "examples/matter-test-setup.svg"
        ).read_bytes()
    )
    assert "3497-011-2332" in page.read_text()
    assert "https://" not in page.read_text()
    assert "<script" not in page.read_text()


def test_log_pair_stays_together(tmp_path):
    log = tmp_path / "uart.log"
    line = f"[I][matter] Matter setup: manual={MANUAL} qr={PAYLOAD}\n"
    log.write_text(line * 2)
    assert read_setup_log(log) == (MANUAL, PAYLOAD)
    log.write_text(line + line.replace(MANUAL, "12345678901"))
    with pytest.raises(ValueError, match="one distinct"):
        read_setup_log(log)


def test_log_without_setup_codes(tmp_path):
    log = tmp_path / "commissioned.log"
    log.write_text("Matter endpoint ready; commissioned=true\n")
    with pytest.raises(ValueError, match="one distinct"):
        read_setup_log(log)


@pytest.mark.parametrize(
    "payload", ["", "https://example.com", "MT:bad", "MT:<script>alert(1)</script>"]
)
def test_invalid_payload(tmp_path, payload):
    with pytest.raises(ValueError, match="MT:"):
        render(payload, tmp_path / "invalid")
    assert not list(tmp_path.iterdir())


def test_invalid_manual_code(tmp_path):
    with pytest.raises(ValueError, match="11-digit"):
        render(PAYLOAD, tmp_path / "invalid", "20202021")
