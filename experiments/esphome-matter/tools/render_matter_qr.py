"""Render the device's own Matter setup payload locally as PNG, SVG and HTML."""

import argparse
import html
import re
from pathlib import Path

import qrcode
import qrcode.image.svg

SETUP_LINE = re.compile(r"Matter setup: manual=([0-9]{11}) qr=(MT:[A-Z0-9.-]+)")
PAYLOAD = re.compile(r"MT:[A-Z0-9.-]{19,250}\Z")


def read_setup_log(path):
    pairs = set(SETUP_LINE.findall(Path(path).read_text(errors="replace")))
    if len(pairs) != 1:
        raise ValueError("The log must contain one distinct Matter setup code pair")
    return next(iter(pairs))


def render(payload, output, manual=None):
    if PAYLOAD.fullmatch(payload) is None:
        raise ValueError("Expected the MT: payload from the device's Matter setup log")
    if manual is not None and re.fullmatch(r"[0-9]{11}", manual) is None:
        raise ValueError("Expected an 11-digit manual pairing code")
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    qr = qrcode.QRCode(
        error_correction=qrcode.constants.ERROR_CORRECT_M, box_size=10, border=4
    )
    qr.add_data(payload)
    qr.make(fit=True)
    png = output.with_suffix(".png")
    svg = output.with_suffix(".svg")
    page = output.with_suffix(".html")
    qr.make_image(fill_color="black", back_color="white").save(png)
    vector = qr.make_image(image_factory=qrcode.image.svg.SvgPathFillImage)
    vector.save(svg)
    code = ""
    if manual:
        pretty = f"{manual[:4]}-{manual[4:7]}-{manual[7:]}"
        code = f'<p>Manual pairing code</p><strong class="manual">{pretty}</strong>'
    page.write_text(
        '<!doctype html><html lang="en"><meta charset="utf-8">'
        '<meta name="viewport" content="width=device-width,initial-scale=1">'
        "<title>Pair with Matter</title><style>"
        "body{font-family:system-ui,sans-serif;text-align:center;background:#fff;color:#111;"
        "margin:2rem auto;max-width:34rem;padding:1rem}"
        "svg{width:min(85vw,360px);height:auto;display:block;margin:auto}"
        ".manual{font-size:2rem;letter-spacing:.06em}code{overflow-wrap:anywhere}"
        "</style><h1>Pair with Matter</h1>"
        "<p>Keep the device and controller on the same LAN. "
        "The commissioning window must be open.</p>"
        + vector.to_string(encoding="unicode")
        + code
        + f"<p><code>{html.escape(payload)}</code></p>"
        "<p>LibreTiny development device with test attestation credentials.</p>"
        "</html>\n",
        encoding="utf-8",
    )
    return png, svg, page


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--payload", help="MT: payload copied from the device log")
    source.add_argument("--log", type=Path, help="Saved UART/ESPHome log")
    parser.add_argument(
        "--output", required=True, type=Path, help="Output filename stem"
    )
    args = parser.parse_args()
    try:
        manual, payload = read_setup_log(args.log) if args.log else (None, args.payload)
        for path in render(payload, args.output, manual):
            print(path)
    except (ValueError, OSError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
