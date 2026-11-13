# Fiesta_clock PCB

[English](README.md)

Płytka 70 × 70 mm dla RP2040-Zero, CAN i DS3231 Mini. Firmware domyślnie używa innego RTC,
dlatego trzeba jawnie wybrać właściwy wariant.

## Przed podłączeniem

- Zasilaj z USB albo instalacji samochodu, nigdy jednocześnie.
- Dla tej płytki wybierz wariant firmware DS3231. Domyślny firmware wybiera PCF8563.
- R9/R10 mają 47 kΩ / 10 kΩ; przeliczenie napięcia w firmware nadal wymaga dostosowania.
- Na prototypie sprawdź temperaturę regulatorów, stabilność zasilania, orientację złącza RTC i układ baterii.

## Montaż i uruchomienie

[Szczegółowa instrukcja montażu (EN)](ASSEMBLY.md) zawiera schematy, podglądy, złącza, dobór
elementów i wymagania produkcyjne. Zachowuje nazwy i numerację z projektu KiCad.

Po sprawdzeniu sprzętu przejdź do [instrukcji firmware](../../src/Fiesta_clock/README.pl.md).
