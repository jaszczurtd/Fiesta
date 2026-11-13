# Clocks PCB

[English](README.md)

Płytka 0.1-review jest pierwszym układem do sprawdzenia montażu i obudowy. Ma 70 × 75 mm i
mieści sterownik wskaźników, CAN, GPS oraz moduły termopar.

## Przed podłączeniem

- Zasilaj z USB albo samochodu, nigdy jednocześnie. Układ nie rozdziela tych źródeł zasilania. Zasilanie samochodowe wymaga bezpieczników i ochrony przed przepięciami.
- CAN i GPS zakładają 12 mm od płytki nośnej do spodu modułu; elementy pod nimi muszą mieścić się poniżej 8 mm. Sprawdź rzeczywiste podstawki i obudowę.
- Sprawdź chłodzenie U3/U4/U8 oraz wymagany typ i ESR kondensatorów regulatorów. Prąd znamionowy regulatora nie określa obciążalności całego montażu.
- Przed uruchomieniem firmware sprawdź numerację złączy i adresy modułów według instrukcji montażu.

## Montaż i uruchomienie

[Szczegółowa instrukcja montażu (EN)](ASSEMBLY.md) zawiera schematy, podglądy, złącza, dobór
elementów i wymagania produkcyjne. Zachowuje nazwy i numerację z projektu KiCad.

Po sprawdzeniu sprzętu przejdź do [instrukcji firmware](../../../src/Clocks/README.pl.md).
