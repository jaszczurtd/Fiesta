# Zależność JaszczurHAL

Fiesta zapisuje konkretny commit JaszczurHAL w submodule `src/JaszczurHAL`.
Korzystają z niego firmware, testy hostowe i generowane zadania VS Code.
Osobny checkout HAL używany przez inne projekty może pozostać na swoim miejscu.

## Przygotowanie projektu

```bash
git clone --recurse-submodules https://github.com/jaszczurtd/Fiesta.git
cd Fiesta
```

Po pobraniu zmian lub przełączeniu wersji Fiesty uruchom:

```bash
./scripts/init_hal_submodule.sh
```

Ten skrypt uruchamia także `runmefirst.sh`. Zatrzymuje się przy lokalnych
zmianach HAL, a czysty checkout przełącza na zapisany commit, bez `--remote`
i wymuszania resetu. W Windows użyj `git submodule update --init --checkout --
src/JaszczurHAL`, po zabezpieczeniu lokalnej pracy w HAL.

HAL przygotowuje własne SDK i zależności źródłowe skryptami `ensure_*`.
Bootstrap Fiesty je uruchamia; samo pobranie submodułu nie instaluje
toolchainów ani tych komponentów.

`runmefirst.sh` przed przygotowaniem projektu usuwa katalogi `build_test`
i `.build` modułów firmware oraz `build` SerialConfiguratora, także przy
pomijaniu testów lub kompilacji. Usuwa to stare ścieżki CMake po migracji.
Katalogi kompilacji HAL, źródła i lokalne ustawienia płytki oraz portu pozostają.
Jeśli budujesz bez `runmefirst.sh`, po zmianie lokalizacji HAL usuń stare
katalogi kompilacji modułów samodzielnie.
SerialConfigurator
nadal pozwala jawnie wskazać `SC_JASZCZURHAL_DIR` podczas rozwoju; CI korzysta
z domyślnego submodułu.

## Aktualizacja HAL

Najpierw opublikuj commit HAL, aby pozostali programiści i CI mogli go pobrać.
Z katalogu głównego Fiesty wybierz go i sprawdź oba projekty razem:

```bash
git -C src/JaszczurHAL fetch origin
git -C src/JaszczurHAL checkout --detach <published-commit>
./runalltests.sh -j8
```

Zbuduj również release i debug wszystkich pięciu modułów oraz sprawdź
aktualność generowanych plików VS Code. Dodaj `src/JaszczurHAL` do indeksu
razem z powiązanymi zmianami Fiesty i po przeglądzie zapisz je we wspólnym
commicie. Zmiana wskaźnika uruchamia istniejące workflow testów modułów
oraz kompilacji firmware.

Przed edycją HAL wewnątrz submodułu utwórz gałąź:

```bash
git -C src/JaszczurHAL switch -c <branch>
```

Zmiany HAL i Fiesty mają osobne commity. Niezapisanych zmian w submodule
ani nieopublikowanego commitu HAL nie da się odtworzyć z zapisanego
wskaźnika Fiesty.
