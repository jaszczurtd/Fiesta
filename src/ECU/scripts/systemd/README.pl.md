# Codzienna kompilacja przez usługę użytkownika systemd

[English](README.md) · [Instrukcja kompilacji](../../../../doc/BUILDING.pl.md)

Timer uruchamia przygotowanie Fiesty, testy i kompilację o 13:00 czasu
lokalnego na Linuksie z systemd, np. Raspberry Pi. `Persistent=true` pozwala
wykonać pominięty przebieg po ponownym uruchomieniu menedżera usług użytkownika.

Użyj osobnego checkoutu: każdy przebieg pobiera repozytorium albo **resetuje
`$FIESTA_DIR` przez `git reset --hard origin/$BRANCH`**, odrzucając lokalne
zmiany śledzonych plików. `runmefirst.sh` usuwa następnie katalogi kompilacji
modułów, inicjalizuje zapisaną rewizję HAL, instaluje brakujące pakiety przez
sudo bez interakcji i wykonuje testy oraz buildy. Lokalne zmiany HAL zatrzymują
inicjalizację submodułu. Katalogi kompilacji HAL i lokalne ustawienia VS Code pozostają.

Runner wysyła e-mail z PASS/FAIL, SHA HEAD, tytułem commita i ostatnimi
80 liniami logu. Załącznik zawiera najwyżej końcowe 512 KB; pełny log zostaje
w `$FIESTA_LOG_DIR` (domyślnie `$HOME/.cache/fiesta-bootstrap/`), a `last.log`
wskazuje ostatni przebieg.

## Pliki i konto usługi

| Plik | Zadanie |
|---|---|
| `fiesta-bootstrap-run.sh` | Aktualizuje checkout, uruchamia setup i wysyła wynik. |
| `send-status.py` | Wysyła SMTP przez bibliotekę standardową Pythona. |
| `fiesta-bootstrap.service` | Usługa użytkownika typu oneshot. |
| `fiesta-bootstrap.timer` | Harmonogram dzienny. |
| `fiesta-bootstrap.env.example` | Szablon ścieżek i konfiguracji SMTP. |

Do `~/.config/systemd/user/` kopiuj tylko `.service` i `.timer`.
Skrypty pozostaw w repozytorium, aby aktualizowały się razem. `ExecStart`
używa `%h/Documents/Fiesta/src/ECU/scripts/systemd/fiesta-bootstrap-run.sh`;
zmień jednostkę, jeśli checkout znajduje się gdzie indziej.

Uruchamiaj `systemctl --user` z konta usługi. Dodanie `sudo` przełącza na
sesję użytkownika root i może dać
`Failed to connect to bus: No medium found`.

## Instalacja

Pracuj z konta, które będzie właścicielem usługi. Pierwszy setup używa
interaktywnego sudo do apt i może potrwać:

```bash
mkdir -p ~/Documents
git clone --recurse-submodules https://github.com/jaszczurtd/Fiesta.git ~/Documents/Fiesta
bash ~/Documents/Fiesta/runmefirst.sh

mkdir -p ~/.config
cp ~/Documents/Fiesta/src/ECU/scripts/systemd/fiesta-bootstrap.env.example \
   ~/.config/fiesta-bootstrap.env
chmod 600 ~/.config/fiesta-bootstrap.env
```

Otwórz `~/.config/fiesta-bootstrap.env` w edytorze i uzupełnij SMTP oraz adres
odbiorcy przed włączeniem timera. Ścieżki w tym pliku muszą być bezwzględne;
szczegóły są poniżej.

Naprawa zależności bez nadzoru wymaga trwałego uprawnienia sudo do potrzebnych
poleceń apt bez pytania o hasło. `sudo -n true` sprawdza tylko bieżący kontekst;
zapamiętane hasło lub zgoda wyłącznie na `true` nie dowodzą, że późniejsze apt
zadziała. Runner sprawdza ten kontekst i używa `sudo -n` do instalacji,
ale nigdy nie zmienia polityki sudo. Lista pakietów jest w
[`ensure-system-dependencies.sh`](../ensure-system-dependencies.sh).

Zainstaluj jednostki i włącz harmonogram:

```bash
mkdir -p ~/.config/systemd/user
cp ~/Documents/Fiesta/src/ECU/scripts/systemd/fiesta-bootstrap.service \
   ~/.config/systemd/user/
cp ~/Documents/Fiesta/src/ECU/scripts/systemd/fiesta-bootstrap.timer \
   ~/.config/systemd/user/
chmod +x ~/Documents/Fiesta/src/ECU/scripts/systemd/fiesta-bootstrap-run.sh
systemctl --user daemon-reload
systemctl --user enable --now fiesta-bootstrap.timer
systemctl --user status fiesta-bootstrap.timer
```

Aby usługi działały po wylogowaniu, włącz lingering dla tego samego konta.
Polecenie może wymagać uprawnień administratora:

```bash
sudo loginctl enable-linger "$USER"
```

## Aktualizacja i kontrola

Opublikuj zmiany przed aktualizacją maszyny kompilującej. Jeśli zmieniły się
wyłącznie skrypty w repozytorium, nie trzeba ponownie instalować jednostek:

```bash
git -C ~/Documents/Fiesta pull --ff-only
systemctl --user start fiesta-bootstrap.service
```

Start usługi od razu wykonuje reset, setup, build i wysyłkę e-maila.
Postęp i harmonogram sprawdzisz poleceniami:

```bash
journalctl --user -u fiesta-bootstrap.service -f
systemctl --user list-timers --all
less ~/.cache/fiesta-bootstrap/last.log
```

Jeśli zmieniono katalog logów, użyj własnej ścieżki. `TimeoutStartSec=30min`
ogranicza czas usługi; gdy maszyna potrzebuje więcej, zmień jednostkę
i przeładuj menedżera użytkownika.

## Konfiguracja SMTP

Użyj danych dostępowych akceptowanych przez dostawcę dla automatycznych
klientów SMTP, np. hasła aplikacji, jeśli jest obsługiwane. Sekret zapisz
w `SMTP_PASS`. Dla przekaźnika pozwalającego temu hostowi wysyłać bez
uwierzytelnienia pozostaw puste `SMTP_USER` i `SMTP_PASS`. Mailer używa
bezpośredniego TLS na porcie 465, a STARTTLS na pozostałych portach.

## Ścieżki środowiskowe

Wartości w `fiesta-bootstrap.env` są dosłowne: nie rozwijają `%h`, `%u`,
`$HOME` ani `~`. Inaczej działają ścieżki `EnvironmentFile=` i `ExecStart=`
w jednostce, gdzie można używać specyfikatorów systemd, takich jak `%h`.

Pozostaw `FIESTA_DIR` nieustawione, aby runner użył `$HOME/Documents/Fiesta`,
albo podaj ścieżkę bezwzględną, np. `FIESTA_DIR=/home/pi/Documents/Fiesta`.
Zastąp `pi` ścieżką właściwego konta. Runner odrzuca nierozwinięte `%...`,
`~` i `$...` w `FIESTA_DIR`, `FIESTA_REPO_URL`, `FIESTA_LOG_DIR` i `BRANCH`
oraz względne `FIESTA_DIR` przed pobieraniem i kompilacją. Jeśli starszy runner
utworzył dosłowny katalog `%h`, sprawdź jego zawartość przed usunięciem
błędnego checkoutu.
