# VP37TestBench

[English](README.md) · [Architektura](../../ARCHITECTURE.pl.md)

To osobne stanowisko pompy z własnym RP2040 i ekranem. Zadanie `VP37: Live viewer` w
projekcie ECU używa innego obrazu: [wariantu BENCH ECU](../ECU/README.pl.md).

## Przygotowanie i obserwacja

Sprawdź połączenia w [device_tree.json](device_tree.json), a zasilanie i pomiar bazowy według
[instrukcji Adjustometera](../Adjustometer/README.pl.md). Otwórz folder `src/VP37TestBench` i
użyj [wspólnej instrukcji builda](../../doc/BUILDING.pl.md). Start pompy obejmuje kalibrację
i może poruszyć nastawnikiem.

Położenie zadaje potencjometr. Zielona kropka ekranu potwierdza napływ ramek Adjustometera;
migająca czerwona oznacza ich brak. Moduł nie obsługuje SerialConfiguratora.

## Sprzęt i wykonanie

VP37TestBench uruchamia `src/common/vp37` na stanowisku pompy: RP2040,
wyświetlacz ST7796S, stopień mocy tego samego typu co ECU i Adjustometer
z tym samym protokołem I²C. Elementy i wartości stopnia opisuje
[fiesta-vp37-power-stage.json](../../src/common/vp37/fiesta-vp37-power-stage.json),
a piny własny `device_tree.json`; odczytuje je
[vp37_power_stage.h](../../src/common/vp37/vp37_power_stage.h).

[vp37_bench_adapter.c](../../src/VP37TestBench/vp37_bench_adapter.c) obsługuje dwa
kanały PWM aktywne stanem niskim, włączenie napędu przez GPIO zamiast PCF8574
oraz I²C bez muteksu, bo magistrala ma jednego użytkownika. Zarówno stanowisko,
jak ECU przez [ecu_hal_config.h](../../src/ECU/ecu_hal_config.h), dołączają
[vp37_hal_config.h](../../src/common/vp37/vp37_hal_config.h). Każda faza I²C ma limit
2 ms, aby zablokowana magistrala nie przekroczyła granicy świeżości sprzężenia
20 ms. Brak limitu zatrzymuje kompilację.

Położenie zadaje potencjometr na pomocniczym wejściu skanu. Rdzeń 0 obsługuje
timery, ekran i logowanie; rdzeń 1 uruchamia pompę i `VP37_process()`.
Ekran odświeża wartości po zmianie. Kropka w rogu świeci stale na zielono,
gdy napływają ramki Adjustometera, i miga na czerwono przy ich braku.
Po zatrzymaniu pompy rdzeń 1 odpytuje status raz na sekundę. Bez pompy start
kończy się timeoutem poziomu odniesienia, a stanowisko nadal działa; pozwala
to sprawdzić podstawowe uruchomienie. Moduł nie obsługuje protokołu konfiguratora.
