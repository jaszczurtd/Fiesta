# Katalog kodów DTC

[English](README.md)

`obd_dtc_catalog.h` wymienia kody usterek, które moduły Fiesty zgłaszają albo
raportują, każdy z dwubajtową wartością SAE J2012 i opisem: ogólne kody
układu napędowego P0xxx, P2xxx i P3xxx z SAE J2012 / ISO 15031-6, ogólne kody
sieciowe raportowane przez projekt oraz własne kody projektu z zakresu
producenta U19xx. ECU nazywa zgłaszane kody w
[`src/ECU/dtc_codes.h`](../../ECU/dtc_codes.h) jako aliasy tego katalogu,
a `dtcManagerGetName()` zwraca opis z katalogu.

`obd_dtc_catalog.c` buduje z tych samych list posortowaną tabelę.
`obdDtcDescription()` znajduje opis wartości, a `obdDtcFormatCode()` drukuje
kod tak, jak pokazuje go skaner: `P0116` albo `U190C`. `obdDtcCatalogCount()`
i `obdDtcCatalogEntry()` pozwalają przejść całą tabelę.

Nowy kod wpisz jako wiersz `X(nazwa, wartość, opis)` do listy, która obejmuje
jego zakres, na właściwym miejscu rosnącego porządku. Kod projektu należy do
`OBD_DTC_FIESTA_CATALOG` i zakresu U19xx. Test hostowy ECU
`test_obd_dtc_catalog` sprawdza porządek, unikalność i aliasy.

`obdDefinitions_sources.txt` to lista jednostek translacji czytana przez
wspólny loader źródeł CMake. Manifesty firmware modułów oznaczonych w rejestrze
`"sharedSources": ["obdDefinitions"]` kompilują te same pliki.

Listę ogólną zestawiono według katalogu rusEFI
(`firmware/controllers/algo/obd_error_codes.h` z
<https://github.com/rusefi/rusefi>, GPL-3.0) i uzupełniono z normy tam, gdzie
ten katalog trzyma kod bez opisu. Numery i opisy są treścią normy; katalog nie
zawiera kodu rusEFI.
