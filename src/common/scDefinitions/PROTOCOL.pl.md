# Protokół Serial Configuratora

[English](PROTOCOL.md) · [Instrukcja konfiguratora](../../SerialConfigurator/README.pl.md)

SerialConfigurator komunikuje się z ECU, Clocks, OilAndSpeed i RTC_Clock przez
USB CDC. Protokół tekstowy obejmuje wykrywanie modułów, odczyt i zmianę
parametrów, uwierzytelnianie oraz wejście do bootloadera przed wgrywaniem.
Logi diagnostyczne współdzielą strumień, ale nie są traktowane jako odpowiedzi.
Adjustometer nie obsługuje protokołu; host pomija go podczas wykrywania.

Literały `SC_*` definiuje [sc_protocol.h](sc_protocol.h). Ten dokument opisuje
zachowanie, a nagłówki i implementacja określają dokładne nazwy i wartości.

## Ramki

Każda linia protokołu ma postać:

```text
$SC,<seq>,<inner>*<crc8>\n
```

- `$SC,` oznacza linię protokołu. Firmware pomija inne linie; host zlicza je
  jako `non_sc` i odrzuca. Dzięki temu `deb()` i `derr()` współdzielą strumień.
- `<seq>` to 16-bitowy numer wybrany przez hosta. Firmware odsyła go,
  umożliwiając przypisanie odpowiedzi i odrzucenie spóźnionej jako `wrong_seq`.
- `<inner>` zawiera polecenie lub odpowiedź, np. `SC_GET_META`.
  Nie może zawierać `*`, `\r` ani `\n`.
- `<crc8>` obejmuje bajty między `$` i `*`. Firmware odrzuca błędne CRC bez odpowiedzi.

Firmware i host kompilują ten sam kodek z HAL
`src/hal/serial/hal_serial_frame.h`. Host odnajduje HAL przez `SC_JASZCZURHAL_DIR`.

## Polecenia

| Grupa | Polecenia | Uwierzytelnienie |
|---|---|---|
| Sesja | `HELLO`, `SC_BYE` | Nie. |
| Odczyt | `SC_GET_META`, `SC_GET_PARAM_LIST`, `SC_GET_VALUES`, `SC_GET_PARAM`, `SC_GET_GPS` (tylko ECU) | Nie. |
| Uwierzytelnianie | `SC_AUTH_BEGIN`, `SC_AUTH_PROVE` | Nie. |
| Zapis parametrów | `SC_SET_PARAM`, `SC_COMMIT_PARAMS`, `SC_REVERT_PARAMS` | Tak. |
| Wgrywanie | `SC_REBOOT_BOOTLOADER` | Tak. |
| Odczyt testów funkcjonalnych ECU | `SC_TEST_LIST`, `SC_TEST_INFO`, `SC_TEST_PARAM`, `SC_TEST_STATUS` | Nie. |
| Sterowanie testami ECU | `SC_TEST_SET`, `SC_TEST_RUN`, `SC_TEST_STOP`, `SC_TEST_SKIP` | Tak. |

`HELLO` zwraca moduł, wersję firmware, identyfikator buildu i UID urządzenia.
UID pochodzi z `hal_get_device_uid_hex()`, pozostałe wartości z
`SC_MODULE_TOKEN_*`, `FW_VERSION` i `BUILD_ID`.

`SC_AUTH_BEGIN` wystawia wyzwanie, a `SC_AUTH_PROVE` odpowiada HMAC-SHA256
z kluczem wyprowadzonym z UID. Wyzwanie jest jednorazowe. Nowe `HELLO` usuwa
uwierzytelnienie. Firmware zlicza błędne odpowiedzi w `auth_failures`, ale
obecnie nie blokuje dalszych prób.

## Stany sesji

| Stan | Wejście | Przyjmowane polecenia |
|---|---|---|
| inactive | Start, `SC_BYE`. | Tylko `HELLO` i `SC_BYE`; reszta dostaje `SC_NOT_READY HELLO_REQUIRED`. |
| active | `HELLO`. | Odczyt i uwierzytelnianie. |
| authenticated | Poprawne `SC_AUTH_PROVE`. | Zapis parametrów i `SC_REBOOT_BOOTLOADER`. |

Sesja nie ma timeoutu i nie wykrywa zamknięcia portu; kończy ją `SC_BYE`.
`HELLO` w aktywnej sesji nadaje nowy identyfikator i usuwa uwierzytelnienie,
ale pozostawia sesję aktywną. Host ponawia `HELLO` tylko po
`SC_NOT_READY HELLO_REQUIRED`. Pozostałe `SC_NOT_READY` pochodzą z aktywnej
sesji, więc nowe `HELLO` usunęłoby uwierzytelnienie potrzebne do ponowienia.

`SC_BYE` odpowiada `SC_OK BYE`, kończy sesję i usuwa oczekujące wyzwanie.
Działa także bez `HAL_ENABLE_CRYPTO`. GUI wysyła je do wykrytych modułów przy
rozłączeniu; starszy firmware odpowiada `ERR UNKNOWN`, co host zapisuje jako ostrzeżenie.

W aktywnej sesji `configSessionTick()` wycisza asynchroniczne logi przez
`hal_debug_set_muted()`. Po zakończeniu sesji logi wracają. Samo wyciszenie
nie synchronizuje obu rdzeni RP2040: HAL obejmuje każde `hal_serial_print`
i `hal_serial_println` muteksem nadawania. Na RP opróżnia też bufor CDC przed
zwolnieniem blokady, żeby ramka opuściła urządzenie przed kolejnym nadawcą.

## Obsługa polecenia w firmware

```text
USB CDC frame
  -> hal_serial_session
  -> hal_serial_commands
  -> hal_command_router
  -> sc_command_service
```

`hal_serial_session` sprawdza ramkę i obsługuje `HELLO`, `SC_BYE`,
`SC_AUTH_BEGIN` oraz `SC_AUTH_PROVE`. `hal_serial_commands` rozdziela nazwę
i argumenty, dodając stan sesji i uwierzytelnienia. Router szuka dokładnej
nazwy i sprawdza dozwolone źródło oraz uwierzytelnienie. Resztę obsługuje
[sc_command_handlers.c](sc_command_handlers.c).

`SC_REBOOT_BOOTLOADER` czeka na wysłanie odpowiedzi przed wejściem do
bootloadera. Moduły rejestrują polecenia tylko dla
`HAL_COMMAND_SOURCE_SERIAL_SESSION`. Dopuszczenie restartu przez BLE lub LoRa
wymagałoby najpierw potwierdzania zakończenia transmisji odpowiedzi na tym łączu.

## Parametry

Moduł definiuje deskryptory w `config.{c,cpp}`. Współdzielony
[sc_param_handlers.c](sc_param_handlers.c) wyszukuje je, sprawdza zakresy,
buduje odpowiedzi `PARAM_LIST`, `PARAM_VALUES` i `PARAM` oraz pakuje
utrwalane wartości do wersjonowanego bloku z CRC32 (PKZIP).

Zapis wymaga uwierzytelnienia i używa kopii roboczej: `SC_SET_PARAM` zmienia
kopię, `SC_COMMIT_PARAMS` sprawdza ją i stosuje, a `SC_REVERT_PARAMS` odtwarza
ją z aktywnych wartości. ECU i RTC_Clock pozwalają zmieniać parametry;
Clocks i OilAndSpeed udostępniają tylko odczyt.

### Dodawanie parametru

Dodaj deskryptor i pole do struktury wartości, a następnie inicjalizację,
wartości domyślne i walidację. Zachowaj zgodność identyfikatora, jednostek
i zakresów z odpowiedziami. Pola `NOT_PERSISTED` nie rozszerzają
zapisywanego bloku. `READ_ONLY` ogranicza zapis przez protokół; samo nie
wyłącza utrwalania pola.

Dla nowego utrwalanego pola:

1. Nadaj nową wersję schematu zapisu. Ustaw `schema_since` nowego deskryptora
   na tę wersję, zachowując wersje wprowadzenia dotychczasowych pól.
2. Przekaż nowy schemat do `sc_param_blob_encode()`. Deskryptor z
   `schema_since` większym od wersji kodowania jest pomijany bez błędu.
3. Przelicz rozmiar bloku z nagłówkiem i CRC, powiększ bufory odczytu/zapisu
   oraz popraw kontrole oczekiwanego rozmiaru i dopuszczalnych długości.
4. Zachowaj odczyt starych schematów. Przed dekodowaniem załaduj wartości
   domyślne dla pól nieobecnych w starym bloku; sprawdź wynik dekodowania.
5. Sprawdź migrację starych danych, zapis i odczyt nowego schematu, błędną
   długość/CRC, odrzucanie wartości i błędy utrwalania. Po commicie ponownie
   załaduj dane i sprawdź nowe pole, nie tylko odpowiedź z kopii roboczej.

Przykładowo [ECU/config.c](../../ECU/config.c) jawnie koduje V2 do 18 B
oraz akceptuje długości V1/V2 przy odczycie. Sam deskryptor V3 nie trafi
więc do zapisywanego V2, nawet jeśli zapis się powiedzie. Przykłady kontroli
są w [testach ECU](../../ECU/tests/test_ecu_params.cpp) i
[testach kodeka](../../SerialConfigurator/tests/test_sc_param.c).

| Plik | Zawartość |
|---|---|
| [sc_protocol.h](sc_protocol.h) | Nazwy poleceń, statusy, znaczniki i formaty odpowiedzi; bez zależności od HAL. |
| [sc_session_vocabulary.h](sc_session_vocabulary.h) | `fiesta_default_vocabulary`: nazwy poleceń Fiesty dla sesji HAL. |
| [sc_param_types.h](sc_param_types.h) | `sc_param_descriptor_t`, flagi `READ_ONLY` i `NOT_PERSISTED`, makra `SC_PARAM_SCALAR_I16(...)`. |
| [sc_param_handlers.h](sc_param_handlers.h) | Wyszukiwanie deskryptorów, zakresy, odpowiedzi i kodowanie danych. |
| [sc_command_handlers.h](sc_command_handlers.h) | Rejestracja poleceń, argumenty, źródła, uwierzytelnienie, odroczony bootloader i operacje testów modułu. |

## Testy funkcjonalne

ECU z `ECU_FUNCTIONAL_TESTS_ENABLED` udostępnia część testów stanowiskowych.
Rejestr `src/ECU/tests.c` oznacza dostępne testy. `dtc`, który pozostawia
usterkę, `kv`, który zapisuje flash, i niekończący się `manual` są tylko
w konsoli. Host buduje widok z odpowiedzi, więc nowy test lub parametr
nie wymaga zmiany aplikacji:

```text
SC_TEST_LIST                -> SC_OK TEST_LIST count=5 names=cyclic,random,top,topzero,pot
SC_TEST_INFO cyclic         -> SC_OK TEST_INFO name=cyclic seq=1 params=cyclic_passes,cyclic_cycles
SC_TEST_PARAM cyclic_passes -> SC_OK TEST_PARAM id=cyclic_passes test=cyclic value=4 min=1 max=100 default=4 unit=count
SC_TEST_SET cyclic_passes 2 -> SC_OK TEST_SET id=cyclic_passes value=2
SC_TEST_RUN all             -> SC_OK TEST_RUN name=all
SC_TEST_STATUS              -> SC_OK TEST_STATUS state=running runs=3 test=cyclic src=sc elapsed_ms=4120 seq=1/4 demand_x10=412 position_x10=405 profile=1 rate=250 cycle=2 cycles=6 pass=1 passes=2 last=pot result=stopped
```

- **Sekwencja:** `seq=1` w opisie włącza test do `SC_TEST_RUN all` w kolejności
  listy; `seq=i/n` w statusie oznacza bieżący krok sekwencji.
- **Parametry:** liczby całkowite z jednostką `count`, `s`, `ms` lub
  `pct_per_s`. Wartość obowiązuje do resetu ECU, potem wraca domyślna.
  Konsola używa tych samych danych: `params`, `set <id> <value>`.
- **Status:** `state`, `runs` (starty od uruchomienia, także jednorazowe),
  `test`, `src` (`sc` lub `console`), `elapsed_ms`, położenie zadane i zmierzone
  w dziesiątych częściach procenta skoku oraz `last`/`result` ostatniego testu.
  Pozostałe klucze opisują postęp, np. profil, szybkość, losowy cel, schodek
  lub fazę potencjometru. Nieznane klucze host pokazuje bez interpretacji.
  ECU odświeża położenie i postęp tylko podczas odpytywania przez hosta,
  w pętli zwalniającej kolejne kroki sterowania. Pierwszy status po przerwie
  może zawierać ostatnio opublikowane dane; kolejne odczyty widzą aktualizację
  po odświeżeniu sterownika (interwał 20 ms). Start i stop widać od razu.
- **Wyniki:** `done`, `ok`, `failed`, `stopped`, `host_lost`, `session_end`,
  `engine_running`.
- **Odmowa:** `SC_BAD_REQUEST unknown_test` dla niedostępnego testu;
  `SC_NOT_READY ENGINE_RUNNING` przy blokadzie od RPM;
  `SC_NOT_READY BUSY`, gdy poprzednie żądanie czeka na rdzeń sterujący.
  Rdzeń odbiera jedno żądanie na takt; zajęte żądanie należy ponowić.

ECU zatrzymuje test konfiguratora po końcu sesji (`SC_BYE`) albo po
`ECU_SC_TESTS_KEEPALIVE_MS` (3 s) bez polecenia testowego, w tym statusu.
Host musi odpytywać status do końca testu. Domyślnie włączony
`ECU_SC_TESTS_RPM_INTERLOCK` odrzuca start i zatrzymuje test po przekroczeniu
`ECU_SC_TESTS_RPM_LIMIT` rpm. Stanowisko podające RPM z generatora przy
nieruchomym silniku buduje się z wyłączoną blokadą. Testy z konsoli należą
do operatora i te zasady ich nie obejmują.

ECU bez testów odpowiada `count=0`; firmware bez poleceń zwraca
`SC_UNKNOWN_CMD`. W obu przypadkach host pokazuje „No tests available".

## Tożsamość urządzenia

Moduły występują jako osobne urządzenia USB, np.:

```text
/dev/serial/by-id/usb-Jaszczur_Fiesta_ECU_DE62A875579C612A-if00
/dev/serial/by-id/usb-Jaszczur_Fiesta_Clocks_E6625887D3475937-if00
```

| Pole USB | Wartość | Źródło |
|---|---|---|
| `iManufacturer` | `Jaszczur` | `identity.usbManufacturer` w `.vscode/jaszczurhal.project.json`. |
| `iProduct` | `Fiesta <Module>` | `identity.usbProduct` w tym samym pliku. |
| `iSerialNumber` | 16 cyfr szesnastkowych UID flash | Odczyt podczas działania przez USB RP w HAL. |

Ten sam UID występuje w `HELLO`. Host szuka `usb-Jaszczur_Fiesta_*`, a po
wgraniu czeka na powrót tego samego UID przed ponownym `HELLO`.

## Wgrywanie firmware

`jh-vscode build` zapisuje `firmware.uf2` w `.build/<target>/<assembly>/`
modułu lub podkatalogu `variants/<variant>/`. Nie tworzy manifestu Fiesty.
Bootstrap Fiesty generuje i sprawdza `firmware.manifest.json` obok obrazu.
Po bezpośrednim buildzie wykonaj osobny
[krok przygotowania manifestu](../../../doc/BUILDING.pl.md#manifest-dla-konfiguratora).

Host wymaga `module_name`, `fw_version`, `build_id` i `sha256` pliku UF2.
`uf2_file` jest opcjonalne przy jawnym wyborze UF2. Jeśli występuje, musi
zawierać nazwę bez ścieżki; jest wymagane do wyboru obrazu na podstawie samego
manifestu. Helper Fiesty zawsze je zapisuje.

Host odrzuca brak wymaganych pól, ścieżkę w `uf2_file` i niezgodną sumę UF2.
Sprawdza sumę ponownie tuż przed restartem. Opcjonalne `signature` jest
odczytywane, ale podpis nie jest weryfikowany; obsługa ed25519 nie została wdrożona.

Konfigurator uwierzytelnia sesję, wysyła `SC_REBOOT_BOOTLOADER`, czeka na
BOOTSEL, kopiuje UF2, czeka na powrót modułu i sprawdza `HELLO`.
Szczegóły opisuje [instrukcja wgrywania](../../SerialConfigurator/README.pl.md#wgrywanie-firmware).
Zadanie VS Code używa `jh-vscode upload`, które sprawdza tożsamość USB portu.
Zasady są w dokumentacji `vscode/README.md` JaszczurHAL.

## Host przy zakłóconej transmisji

- **Błędne ramki:** gdy znika początkowe `$`, host odtwarza je, nadal sprawdza
  CRC i numer sekwencji oraz zlicza `repaired`. Pierwsza uszkodzona ramka
  skraca oczekiwanie do 60 ms zamiast `SC_TRANSPORT_PRIMARY_TIMEOUT_MS`
  (400 ms). Firmware sam nie ponawia odpowiedzi; host otwiera port ponownie
  i powtarza żądanie.
- **Spóźnione odpowiedzi:** wcześniejszy numer sekwencji daje `wrong_seq`;
  odpowiedź jest pomijana.
- **Logi:** linie bez `$SC,` zwiększają `non_sc` i nie trafiają do parsera odpowiedzi.

Timeout raportuje liczniki bajtów, linii, `non_sc`, uszkodzonych ramek,
`wrong_seq`, `repaired` i przepełnień oraz pierwsze nieoczekiwane linie
skrócone do 80 znaków. `SC_DEBUG_DEEP` w
[config.h](../../SerialConfigurator/src/config.h) włącza logowanie każdej
ramki i skanowania dysków na stderr.
