#!/usr/bin/env python3
"""Send a status email for the Fiesta daily bootstrap runner.

Config comes from environment variables (exported by the systemd unit via
EnvironmentFile=fiesta-bootstrap.env):

    SMTP_HOST, SMTP_PORT (default 587), SMTP_USER, SMTP_PASS
    MAIL_FROM (default: SMTP_USER), MAIL_TO (required)

Port 465 triggers implicit TLS (SMTPS); any other port uses STARTTLS.
"""
from __future__ import annotations

import argparse
import os
import smtplib
import ssl
import sys
from email.message import EmailMessage
from pathlib import Path


ATTACHMENT_LIMIT_BYTES = 512 * 1024  # keep mails comfortably small


def build_message(subject: str, body_file: Path, attachments: list[Path]) -> EmailMessage:
    mail_from = os.environ.get("MAIL_FROM") or os.environ.get("SMTP_USER")
    mail_to = os.environ.get("MAIL_TO")
    if not mail_from:
        raise SystemExit("MAIL_FROM / SMTP_USER not set")
    if not mail_to:
        raise SystemExit("MAIL_TO not set")

    msg = EmailMessage()
    msg["From"] = mail_from
    msg["To"] = mail_to
    msg["Subject"] = subject
    msg.set_content(body_file.read_text(errors="replace"))

    for path in attachments:
        if not path.is_file():
            continue
        data = path.read_bytes()
        if len(data) > ATTACHMENT_LIMIT_BYTES:
            data = b"(truncated, tail only)\n" + data[-ATTACHMENT_LIMIT_BYTES:]
        msg.add_attachment(
            data,
            maintype="text",
            subtype="plain",
            filename=path.name,
        )
    return msg


def send(msg: EmailMessage) -> None:
    host = os.environ.get("SMTP_HOST")
    if not host:
        raise SystemExit("SMTP_HOST not set")
    port = int(os.environ.get("SMTP_PORT", "587"))
    user = os.environ.get("SMTP_USER")
    pw = os.environ.get("SMTP_PASS")

    ctx = ssl.create_default_context()
    if port == 465:
        with smtplib.SMTP_SSL(host, port, context=ctx, timeout=30) as s:
            if user:
                s.login(user, pw or "")
            s.send_message(msg)
    else:
        with smtplib.SMTP(host, port, timeout=30) as s:
            s.ehlo()
            s.starttls(context=ctx)
            s.ehlo()
            if user:
                s.login(user, pw or "")
            s.send_message(msg)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--subject", required=True)
    parser.add_argument("--body-file", required=True, type=Path)
    parser.add_argument("--attach", action="append", default=[], type=Path)
    args = parser.parse_args()

    msg = build_message(args.subject, args.body_file, args.attach)
    send(msg)
    print(f"[mail] sent: {args.subject}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except SystemExit:
        raise
    except Exception as exc:
        print(f"[mail] send failed: {exc}", file=sys.stderr)
        sys.exit(1)
