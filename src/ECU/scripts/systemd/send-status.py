#!/usr/bin/env python3
"""Send Fiesta status notification via SMTP or Telegram.

Backend is selected with:

    NOTIFY_BACKEND=smtp       # default
    NOTIFY_BACKEND=telegram

SMTP configuration:
    SMTP_HOST
    SMTP_PORT
    SMTP_SECURITY=none|starttls|ssl
    SMTP_USER
    SMTP_PASS
    MAIL_FROM
    MAIL_TO

Telegram configuration:
    TELEGRAM_BOT_TOKEN
    TELEGRAM_CHAT_ID
    TELEGRAM_SPLIT_CHARS      # optional, default 3500
"""

from __future__ import annotations

import argparse
import json
import os
import smtplib
import ssl
import sys
import urllib.error
import urllib.request
from email.message import EmailMessage
from pathlib import Path


ATTACHMENT_LIMIT_BYTES = 512 * 1024


def split_text(text: str, limit: int) -> list[str]:
    if limit <= 0 or len(text) <= limit:
        return [text]

    chunks: list[str] = []
    current = ""

    for line in text.splitlines(keepends=True):
        while len(line) > limit:
            if current:
                chunks.append(current.rstrip())
                current = ""

            chunks.append(line[:limit].rstrip())
            line = line[limit:]

        if len(current) + len(line) > limit:
            if current:
                chunks.append(current.rstrip())
            current = line
        else:
            current += line

    if current:
        chunks.append(current.rstrip())

    return chunks


def build_email(
    subject: str,
    body: str,
    attachments: list[Path],
) -> EmailMessage:
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
    msg.set_content(body, charset="utf-8")

    for path in attachments:
        if not path.is_file():
            continue

        data = path.read_bytes()

        if len(data) > ATTACHMENT_LIMIT_BYTES:
            data = (
                b"(truncated, tail only)\n"
                + data[-ATTACHMENT_LIMIT_BYTES:]
            )

        msg.add_attachment(
            data,
            maintype="text",
            subtype="plain",
            filename=path.name,
        )

    return msg


def send_smtp(msg: EmailMessage) -> None:
    host = os.environ.get("SMTP_HOST")
    if not host:
        raise SystemExit("SMTP_HOST not set")

    port = int(os.environ.get("SMTP_PORT", "587"))
    user = os.environ.get("SMTP_USER")
    password = os.environ.get("SMTP_PASS")
    security = os.environ.get("SMTP_SECURITY", "starttls").lower()

    ctx = ssl.create_default_context()

    if security == "ssl":
        with smtplib.SMTP_SSL(
            host,
            port,
            context=ctx,
            timeout=30,
        ) as smtp:
            if user:
                smtp.login(user, password or "")
            smtp.send_message(msg)

    elif security == "starttls":
        with smtplib.SMTP(host, port, timeout=30) as smtp:
            smtp.ehlo()
            smtp.starttls(context=ctx)
            smtp.ehlo()

            if user:
                smtp.login(user, password or "")

            smtp.send_message(msg)

    elif security == "none":
        with smtplib.SMTP(host, port, timeout=30) as smtp:
            smtp.ehlo()

            if user:
                smtp.login(user, password or "")

            smtp.send_message(msg)

    else:
        raise SystemExit(
            f"Invalid SMTP_SECURITY={security!r}; "
            "expected: none, starttls, ssl"
        )


def telegram_send_message(text: str) -> None:
    token = os.environ.get("TELEGRAM_BOT_TOKEN")
    chat_id = os.environ.get("TELEGRAM_CHAT_ID")

    if not token:
        raise SystemExit("TELEGRAM_BOT_TOKEN not set")
    if not chat_id:
        raise SystemExit("TELEGRAM_CHAT_ID not set")

    url = f"https://api.telegram.org/bot{token}/sendMessage"

    payload = json.dumps(
        {
            "chat_id": chat_id,
            "text": text,
        }
    ).encode("utf-8")

    request = urllib.request.Request(
        url,
        data=payload,
        headers={"Content-Type": "application/json"},
        method="POST",
    )

    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            result = json.loads(response.read().decode("utf-8"))

    except urllib.error.HTTPError as exc:
        body = exc.read().decode("utf-8", errors="replace")
        raise RuntimeError(
            f"Telegram HTTP {exc.code}: {body}"
        ) from exc

    if not result.get("ok"):
        raise RuntimeError(
            f"Telegram API error: {result}"
        )


def send_telegram(subject: str, body: str) -> None:
    limit = int(os.environ.get("TELEGRAM_SPLIT_CHARS", "3500"))

    # Zostawiamy zapas względem limitu Telegrama 4096 znaków.
    parts = split_text(body, limit)
    total = len(parts)

    for index, part in enumerate(parts, start=1):
        if total == 1:
            header = subject
        else:
            header = f"{subject} ({index}/{total})"

        text = f"{header}\n\n{part}"
        telegram_send_message(text)

        print(
            f"[telegram] sent: {header} "
            f"({len(text)} chars)"
        )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)

    parser.add_argument("--subject", required=True)
    parser.add_argument(
        "--body-file",
        required=True,
        type=Path,
    )
    parser.add_argument(
        "--attach",
        action="append",
        default=[],
        type=Path,
    )

    args = parser.parse_args()

    body = args.body_file.read_text(
        encoding="utf-8",
        errors="replace",
    )

    backend = os.environ.get(
        "NOTIFY_BACKEND",
        "smtp",
    ).lower()

    if backend == "smtp":
        msg = build_email(
            args.subject,
            body,
            args.attach,
        )

        send_smtp(msg)
        print(f"[smtp] sent: {args.subject}")

    elif backend == "telegram":
        send_telegram(
            args.subject,
            body,
        )

        if args.attach:
            print(
                "[telegram] note: attachments are ignored; "
                "the notification body already contains the log tail"
            )

    else:
        raise SystemExit(
            f"Invalid NOTIFY_BACKEND={backend!r}; "
            "expected: smtp or telegram"
        )

    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except SystemExit:
        raise
    except Exception as exc:
        print(
            f"[notify] send failed: {exc}",
            file=sys.stderr,
        )
        sys.exit(1)

