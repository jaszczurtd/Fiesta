# Fiesta

[English](README.md)

Firmware i elektronika do Forda Fiesty 1.8 diesel z niestandardowym silnikiem
łączącym części 1.8D z wtryskiem pośrednim i 1.8TDDI z wtryskiem bezpośrednim
oraz pompą VP37. [JaszczurHAL](https://github.com/jaszczurtd/JaszczurHAL)
zapewnia obsługę sprzętu i narzędzia. Autor: Marcin „Jaszczur" Kielesiński.

Projekt jest na etapie prototypu. Cały system nie został sprawdzony jako
zintegrowana instalacja w samochodzie drogowym. Próby ECU i VP37 odbywają się
na osobnym silniku na stalowej ramie. Clocks i funkcje nadzorcze ECU były
używane w samochodzie bez sterowania VP37.

## Wybierz cel

| Chcę... | Strona |
| --- | --- |
| Poznać stan projektu, ograniczenia i dalsze prace | [Stan projektu i sprzętu](doc/STATUS.pl.md) |
| Uruchomić ECU i zobaczyć pomiary | [Pierwsze uruchomienie ECU](src/ECU/README.pl.md) |
| Obejrzeć lub porównać zapisane pomiary | [Viewer VP37](tools/vp37-viewer/README.pl.md) |
| Zbudować i wgrać firmware | [Kompilacja i debugowanie](doc/BUILDING.pl.md) |
| Zrozumieć moduły i ich połączenia | [Architektura](ARCHITECTURE.pl.md) |
| Zmienić ustawienia lub uruchomić test wyjść | [SerialConfigurator](src/SerialConfigurator/README.pl.md) |

## Pierwsze kroki

```bash
git clone --recurse-submodules https://github.com/jaszczurtd/Fiesta.git
cd Fiesta
```

Przed uruchomieniem `runmefirst.sh` przeczytaj [instrukcję przygotowania
środowiska](doc/BUILDING.pl.md): skrypt instaluje zależności i usuwa katalogi
kompilacji modułów. Po aktualizacji istniejącego projektu uruchom
`./scripts/init_hal_submodule.sh`, aby wybrać commit HAL zapisany w Fieście.
Skrypt zatrzyma się, jeśli HAL zawiera lokalne zmiany.

## Dokumentacja

- [Architektura i komunikacja](ARCHITECTURE.pl.md)
- [Kompilacja, wgrywanie i debugowanie](doc/BUILDING.pl.md)
- [Testy hostowe i CI](doc/TESTING.pl.md)
- [Przeglądarka pomiarów VP37](tools/vp37-viewer/README.pl.md)
- [Aktualizacja JaszczurHAL](DEPENDENCIES.pl.md)
- [Protokół Serial Configuratora](src/common/scDefinitions/PROTOCOL.pl.md)
- [Zakres MISRA i zasady przeglądu](MISRA.pl.md)
- [Codzienna kompilacja przez systemd](src/ECU/scripts/systemd/README.pl.md)
- [Galeria i zdjęcia stanowiska](doc/GALLERY.pl.md)

Szczegóły zgodności sprzętu i granice walidacji opisuje [stan projektu](doc/STATUS.pl.md).
