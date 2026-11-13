# Adjustometer PCB

[English](README.md)

Prototypowa płytka czujnika położenia VP37, 45 × 36 mm, z RP2040-Zero i oscylatorem Hartleya.
Instrukcja firmware opisuje pomiar bazowy i ważność sprzężenia.

## Przed podłączeniem

- Podaj stabilizowane 5 V i 12 V ze wspólną masą. U1 zasila wyłącznie tor oscylatora napięciem 8 V.
- Odłącz zewnętrzne 5 V przed zasileniem RP2040-Zero z USB: VBUS USB i pin 5 V modułu są połączone.
- Piny J1 1-5 odpowiadają pinom wiązki pompy 1, 2, 3, 4 i 7. J2 1-5 to 5 V, GND, SDA, SCL i 12 V. Orientację sprawdź w instrukcji montażu.
- Resetuj przy wyłączonym napędzie i nieruchomym mechanizmie. Zaczekaj na ważny pomiar bazowy, potem zresetuj ECU.

## Montaż i uruchomienie

[Szczegółowa instrukcja montażu (EN)](ASSEMBLY.md) zawiera schematy, podglądy, złącza, dobór
elementów i wymagania produkcyjne. Zachowuje nazwy i numerację z projektu KiCad.

Po sprawdzeniu sprzętu przejdź do [instrukcji firmware](../../../src/Adjustometer/README.pl.md).
