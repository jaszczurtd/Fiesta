# ECU PCB 3

[English](README.md)

To nowa dwuwarstwowa płytka ECU do obudowy Ford EEC-V. Przed użyciem wymaga dostosowania firmware.

## Przed podłączeniem

- Obecne assembly ECU opisują starsze połączenia. PCB 3 ma Adjustometer na I²C0 (GP0/GP1), ekspander na I²C1 (GP2/GP3) i wejście wtrysku na GP4.
- TC4427 nie odwraca sygnału: LOW wyłącza MOSFET. Dostosuj polaryzację wyjść i sprawdź stan przy starcie oraz resecie przed podłączeniem obciążenia.
- Pozostaje walidacja elektryczna i termiczna. Moduły CAN zachowują zasilanie 3,3 V, poza zakresem podanym dla TJA1050. Wymaga to sprawdzenia prototypu.
- Zasilanie, styki dzielące prąd i wymagane parametry elementów opisują mapa złącza i instrukcja montażu. Wymagana miedź zewnętrzna ma 70 µm; projekt ścieżek nie jest pomiarem dopuszczalnego prądu ciągłego.

## Montaż i uruchomienie

[Szczegółowa instrukcja montażu (EN)](ASSEMBLY.md) zawiera schematy, podglądy, złącza, dobór
elementów i wymagania produkcyjne. Zachowuje nazwy i numerację z projektu KiCad.

Po sprawdzeniu sprzętu przejdź do [instrukcji firmware](../../src/ECU/README.pl.md).
