# Unde e proiectul, pe scurt

Scris ca o sesiune nouă (sau un model nou) să poată continua fără să recitească
DEVLOG-ul de 4000 de rânduri. Starea de la 15.09.2026, după commit-ul „portul"
(al şaselea din arcul de mecanică).

## Unde stă, şi de ce NU stă lângă celelalte proiecte

`C:\Users\besli\Documents\Unreal Projects\PirateSeas` — unde îl pune Unreal
implicit. Există o scurtătură către el în `MyWork\Apps\games\PirateSeas.lnk`,
lângă restul proiectelor.

**Nu se mută în `MyWork`, şi asta e o decizie luată, nu o scăpare** (15.09.2026,
owner-ul întrebat direct). `MyWork` e sincronizat cu Google Drive, iar din cele
2,95 GB ale proiectului **2,83 GB sunt ieşiri de build** pe care motorul le
rescrie la fiecare compilare: `Intermediate` 2,42 GB, `Saved` 0,41 GB,
`Binaries` 0,06 GB. Proiectul propriu-zis e ~120 MB. Într-un folder sincronizat
asta ar însemna 2,4 GB urcaţi după fiecare build (într-o singură zi de lucru au
fost vreo douăzeci), plus riscul ca Drive să ţină un fişier blocat exact când
scrie compilatorul în el. Şi ar dubla singur dimensiunea întregului `Apps`.

Dacă totuşi se mută vreodată: mai întâi şterge folderele regenerabile (sunt
toate în `.gitignore`), apoi mută ~120 MB, apoi pune-le înapoi ca JONCŢIUNI
către un folder local nesincronizat — şi VERIFICĂ pe teren că Drive chiar
ignoră joncţiunile, nu presupune. Şi ai de reparat calea absolută scrisă în
cinci scripturi din `Scripts/` plus una îngropată în `Scripts/ship.blend`.

## Ce e construit

**Bucla completă:** navighezi cu pânză şi cârmă (W/S trim, A/D cârmă, mouse doar
camera), tragi salve pe bord (Q/E, Shift = tir înalt), încasezi avarii pe zone,
te scufunzi în patru faze şi reapari. Un căpitan inamic AI navighează, schimbă
bordul, trage şi ştie de maluri sub vânt. Insule cu plajă, hău, vegetaţie şi
eşuare. Vânt care conduce şi vela, şi marea.

**Grafica** (arc încheiat, se va relua mai târziu): 21 de texturi sintetizate cu
numpy, trei plane de proiecţie în spaţiu de instanţă, normale din gradienţi,
mare cu şase valuri Gerstner care urmează vântul, spumă pe creste keyed la sigma,
siaj cu firimituri + guler + braţe Kelvin, stropi, surf la ţărm, cordaj cu LOD,
vânt în plante şi greement, `-Hour=` pentru ora din zi.

## Cum se lucrează aici (obligatoriu)

- **Totul prin script.** C++ în `Source/PirateSeas`, materiale ca grafuri Python
  în `Scripts/*.py`, meshuri din Blender, texturi din numpy. Editorul nu se
  deschide niciodată. Python-ul Unreal NU poate scrie noduri Blueprint sau
  Niagara.
- **Build:** `Build.bat PirateSeasEditor Win64 Development -Project=<abs>.uproject`
- **Un script de material:** `.\Scripts\run_py.ps1 -Script <x>.py -Tag "<filtru>"`
- **Build de sine stătător (exe):** `RunUAT.bat BuildCookRun -project=<abs> -noP4
  -platform=Win64 -clientconfig=Development -cook -build -stage -pak -archive
  -archivedirectory=<abs>\Packaged`. Iese la
  `Packaged\Windows\PirateSeas.exe`, ia ACELEAŞI flag-uri ca editorul, şi îşi
  scrie logul la `Packaged\Windows\PirateSeas\Saved\Logs\`. `Packaged/` e în
  `.gitignore`.
- **Coca pe care o loveste ghiuleaua** (25.09): `Scripts/ship.py` exporta si
  `SM_PirateHull.fbx` (pielea inchisa a cocii, cu parapet). Dupa orice
  schimbare a cocii: Blender → `run_py.ps1 -Script reimport_hull.py` (o data;
  moare dupa import, e normal) → `run_py.ps1 -Script hull_collision.py` DE DOUA
  ORI: prima seteaza complex-as-simple si verifica varful (parapet) si numarul
  de triunghiuri; a doua, prin linia ei `HULLCOL before`, e adevarul de pe
  disc - in acelasi proces `load_asset` intoarce obiectul din memorie. Si in
  joc fiecare nava spune la BeginPlay `SHIPLOG ... shothull=ok`; cheia
  `shothull_ok` din suita e 0 daca vreo coca e transparenta.
- **Sunetele** (25.09): `python Scripts/sounds.py` scrie `Scripts/Sounds/S_*.wav`
  (sintetizate, cu samanta; UNDELE SUNT IN GIT: poarta le regenereaza si cere
  octetii din arbore - dupa orice schimbare in `sounds.py` rulezi generatorul,
  reimporti si comiti .wav-urile cu .uasset-urile), apoi
  `run_py.ps1 -Script import_sounds.py` de doua ori (importa undele si face
  `ATT_S_*`; a doua rulare citeste de pe disc). Modul de joc le tine ca referinte
  TARI in constructor - altfel cook-ul nu le ia si pachetul e mut.
- **Probe (nu porti):** `python tools/probe_hull_hits.py [--band=LO,HI]` - continua
  fiecare lovitura de pe cutia de coliziune pana la lemnul din `ship.py` si
  reclasifica zona. Ruleaza motorul, cateva minute. Scrisa 25.09 pentru decizia
  din `OWNER_VERIFY` 38 (banda tunurilor nu mai contine tunurile).
- **Porţile:**
  - `python tools/ci_checks.py` — fără motor, rulează şi în CI hosted
  - `python tools/ci_measure.py` — 74 scenarii headless vs `tools/measurement_baseline.json`
  - `python tools/ci_measure.py --record` — rescrie linia de bază, DELIBERAT, în
    acelaşi commit care o mişcă
  - `python tools/png_diff.py a.png b.png x0 y0 x1 y1` — compară o casetă
- **Reproductibilitate:** `-UseFixedTimeStep -FPS=60 -ShipSeed=1` plus vântul, pe
  fiecare rulare. Fără toate patru, două rulări identice dau numere diferite.
  Cu `-Port=1`, şi `-Ledger=0`: altfel a doua rulare pleacă din cartea primei.
- **Capturi:** scoate `-NullRHI` şi adaugă `-ShipShots=20,21 -ShipShotCam=beam`.
  `-Ledger=0` RĂMÂNE: fără el, captura pleacă din cartea navei a owner-ului.
  Prima rulare după ce reconstruieşti un material arată starea VECHE — rulează
  de două ori.
- **PowerShell:** un flag cu zecimale se pune între ghilimele
  (`"-EnemyRigDamage=0.4"`), altfel ajunge `0` şi rularea „merge" cu alt
  rezultat. Şi NICIODATĂ `python - @'...'@` — deschide un REPL şi atârnă
  pentru totdeauna; scripturile de analiză se scriu în fişiere.

## Reguli care au costat ceva ca să fie învăţate

1. **O poartă care nu poate ieşi roşie e cel mai grav defect din proiect.** Un
   contor de eveniment rar se ZĂVORĂŞTE (membru cumulativ), nu se eşantionează:
   contoarele de siaj erau locale pe cadru şi linia se tipărea o dată pe secundă.
2. **Proba e MUTAŢIA:** pune defectul la loc, măsoară, scoate-l, măsoară. Un zero
   fără perechea asta nu înseamnă nimic.
3. **O valoare măsurată bate una derivată**, chiar când derivarea e corectă —
   banda de expunere calculată din principii a făcut din apus o siluetă.
4. **Un parametru pe care nimeni nu-l împinge e un buton mort**, şi a seta unul
   pe care materialul nu-l are e tăcut un no-op. Citeşte ÎNAPOI.
5. **Diferenţa de imagine** poate răspunde „e deplasat?", nu „se vede mişcând?"
   (TAA suprimă exact zonele cu vectori de mişcare).
6. **Sondele de depanare se scalează cu scena:** emissive 1,0 e invizibil la
   110.000 lux.
7. **Un scenariu cu două jumătăţi care trebuie să iasă DIFERIT** e singura
   probă că o mecanică înseamnă ceva: convoiul a ieşit identic de pe ambele
   părţi ale vântului de trei ori la rând (raider prea lent, negustor prea
   rapid, ţintă care sărea). Fiecare din ele ar fi trecut o poartă „nu crapă".
8. **Aceeaşi polară nu poate prinde aceeaşi polară.** O vânătoare se
   dimensionează măsurând-o (viteză, timp până în radă, timp până la prima
   salvă), nu alegând numere care sună bine.
9. **Un minim peste toate cocile citeşte coca greşită.** `gun_crew_min` e 0,25
   în ambele scenarii de convoi fiindcă negustorul are 14 oameni şi stă pe
   `MinGunCrewFactor` — raider-ul, care e la 1,00, nu poate fi raportat
   niciodată de cheia aia. Citeşte DUPĂ NUME de pe linia coçii care te
   interesează (`enemy_rig_quit`, `enemy_gun_crew_quit`).
10. **Un contor care creşte cât timp nava nu realizează nimic măsoară lungimea
   rulării.** Un raider refuzată stătea lângă pradă până la final: 13.224 de
   ticuri de nimic. Orice stare „stau şi aştept" are nevoie de o ieşire.
11. **Un timer oprit la sfârşitul misiunii nu vede ce urmează după ea.**
   Verificarea „a ajuns prada în radă?" a fost scrisă în `SampleWeatherGauge`,
   care se opreşte la `bMissionOver` — iar convoiul se decide la 71 s, în timp
   ce o pradă are nevoie de 356. Logul a spus-o într-un rând: o pradă la ŞASE
   metri de chei lângă `landed=0`. Orice lucru care se întâmplă după deznodământ
   are nevoie de propriul timer.
12. **Întinderea rulării nu repară o geometrie greşită.** `prizes_refused` a
   citit 0 la 400 s şi la 500 s; cauza nu era timpul, ci că ţinta rămăsese la
   350 m SUB VÂNT, ceea ce polara proiectului preţuieşte la sute de secunde.
   Refuzul s-a făcut determinist, nu răbdător.
13. **Ce toleră editorul, cook-ul refuză.** Prima împachetare a picat pe opt
   erori care se scriau în log de la începutul proiectului şi pe care nimeni nu
   le citea: apeluri de fizică din constructori (mutate pe `BodyInstance` —
   zero numere mişcate) şi profilul de coliziune al apei, pe care plugin-ul şi-l
   adaugă singur doar dacă îl porneşti din INTERFAŢA editorului. Un proiect
   făcut integral prin script nu trece niciodată pe acolo.
14. **Un număr derivat se scrie cu ingredientele pe aceeaşi linie**
   (`value=1200 cargo=1200 hull=1.00`), şi suma lui se re-derivă într-o
   fixtură (`purse_balances`): altfel o greşeală de transcriere în bani e o
   cifră pe care trebuie s-o vadă cineva cu ochiul.

## Arcul de MECANICĂ (în curs)

Grafica e parcată de owner („deocamdată arată ok"). Owner-ul a ales, prin
întrebare directă: **obiective**, apoi **echipaj + reparaţii**, apoi **economie
+ progresie**. **Abordajul e amânat explicit** („mai vedem cu abordajul") — nu-l
construi nesolicitat. Autonomie „ca până acum": deciziile de design le iau eu,
măsor tot, judecăţile vizuale merg în OWNER_VERIFY.

Livrat:
1. **Apartenenţa** (`ed65e93`): `EShipAllegiance {Player, Crown, Merchant}` pe
   clasă, `IsHostileTo()` într-un singur loc. Toate cele 10 scenarii nemişcate.
2. **Convoiul**: `AMerchantShipPawn` (încărcat, 55% viteză), coborârea
   pavilionului (`Strike()` pe ramura de avarii), rada, `-Convoy= -RaiderSide=`,
   misiune LUAT / A TRECUT / NEREZOLVAT cu o singură linie `CONVOYLOG MISSION`,
   contoare zăvorâte gauge/lee/beat. Perechea `convoy_weather` / `convoy_lee`
   trebuie să iasă DIFERIT. Şi două reparaţii la căpitan, fără de care convoiul
   era de neluat de pe nicio parte: urmărirea unui fugar (histereză pe distanţă)
   şi ţinta păstrată până iese din luptă.

3. **Echipajul, felia 1**: `Hands`/`Casualties` pe `AShipPawn` (pierderi pe
   zonă la fiecare lovitură, niciodată la eşuare), `RepairShare` împarte oamenii
   între tunuri (reîncărcarea scade sub 48 de oameni) şi reparaţii (cârma, apoi
   catargul mai rău, plafon 0,85, coca nu), tasta R / `-ShipRepairShare=`,
   doctrina căpitanului (`-AIRepair=0` o opreşte), bara HANDS, o linie
   `CREWLOG` per cocă la quit. Perechea `crew_repair` / `crew_fight` trebuie să
   difere (prima salvă 224,6 vs 255,9). Şi a treia reparaţie la căpitan:
   unghiul de apropiere în AFARA distanţei de menţinere (înăuntru neschimbat).

4. **Prăzile, felia 1 de economie**: `CargoValue` pe negustor, valoarea
   încasată ÎN CLIPA coborârii pavilionului (`valoare = marfă × cocă rămasă`),
   `Purse`/`PrizesTaken`/`PrizeValueMax` zăvorâte pe game mode, o linie
   `PRIZELOG` per pradă cu ingredientele lângă rezultat, o linie `PURSE` la
   fiecare quit (zero numărat), rândul PURSE în panou, `-ConvoyCargo=`,
   `-AIAimHigh=1|0` (mută knob-ul EXISTENT `FireHighAboveRig`, fără ramură
   nouă). Perechea `prize_rig`/`prize_hull`: 1200 contra 696.
   Patru designuri, doi judecători; ambii au ales „prada" şi ambii au tăiat
   felia la VALOARE, fără stăpânire, fără echipaj de pradă, fără stare de AI.

5. **Stăpânirea prăzii**: `DetachPrizeCrew`/`ManAsPrize` pe `AShipPawn`
   (`HandsInPrizes` zăvorât, SEPARAT de `Casualties`), `SamplePrizes()` pe
   timer propriu NEŞTERS la sfârşitul misiunii, timp alături CUMULAT (nu
   neîntrerupt), podea de 20 de oameni pe punte, doctrina căpitanului
   `-AIPrize=1` **implicit STINSĂ**, renunţare după 40 s degeaba,
   `-PrizeCrew= -PrizeRangeM= -PrizeBoatSeconds= -EnemyHands=` (ultimul PĂZIT
   pe apartenenţă). Perechea e `prize_rig` (stinsă) contra `prize_manned`
   (aprinsă): 0 contra 2 prăzi stăpânite, 0 contra 24 de oameni plecaţi,
   reîncărcare 1,00 contra 0,75. Plus `prize_shorthanded` pentru podea.

6. **Portul**: `-Port=1` pune o radă SUB VÂNTUL convoiului; o pradă cu echipaj
   la bord face vela spre ea pe exact drumul unui negustor (`TickMerchant` +
   `SetDestination`), iar la sosire banii devin reali (`Landed`) şi oamenii se
   întorc (`HandsReturned`). Perechea `prize_home`/`prize_noport`: 1 contra 0
   prăzi ajunse, 1200 contra 0 la chei, 12 contra 0 oameni întorşi — şi
   `purse_end` IDENTIC, fiindcă valoarea se stabileşte la pavilion.

7. **Refitul in port**: punga CUMPARA. Un om 20, un punct de coca 0,5, platit
   din `Landed` (banii ajunsi la chei), un om sau 20 de puncte la fiecare
   jumatate de secunda petrecuta in rada. Doctrina `-AIRefit=1` (implicit
   STINSA) cere si bani in vistierie, nu doar avarii: fara regula asta o nava
   lovita pleca spre port in primele secunde si nu mai vana niciodata.
   Perechea `refit_on`/`refit_off`, dimensionata dintr-o cronologie MASURATA
   (prada acasa la 359, rada la 764, gata dupa 21,5 s de cumparaturi - de aia
   ruleaza 800 s): 640 cheltuiti contra 0 - 20 de ghiulele, 20 de oameni si 400
   de coca contra nimic (scria 600 si 776, de dinainte ca ghiulelele sa se
   cumpere primele).

8. **Magazia**: `-Shot=N` / `-EnemyShot=N`; fara flag sunt **40** de ghiulele
   (decizia owner-ului din 16.09), si `=0` o face fara fund. Permisiunea e PE
   TUN din 19.09: cu trei ghiulele si patru tunuri pleaca o salva de trei.
   Bucla trage doua numere aleatoare per tun din sirul lui `-ShipSeed`, deci un
   tun sarit ar muta fiecare ghiulea de dupa - de aceea refuzul per tun sta DUPA
   extrageri, nu inainte de bucla. Refuzurile se numara pe
   REPRIZE, nu pe cadre (AI-ul cere sa traga la fiecare tick). Portul vinde
   ghiulele cu 2 si le cumpara PRIMELE. Perechea `magazine_dry`/`magazine_full`
   schimba si deznodamantul: 4 ghiulele -> convoiul TRECE, 40 -> convoiul e LUAT.

9. **Magazia FINITA IMPLICIT** (decizie owner, 16.09): 40 de ghiulele, zece
   salve. Numarul ales din suita - inamicul trage 0-20 aproape peste tot, 24
   intr-o urmarire lunga, 44 in duelul de 360 s, deci 40 se goleste exact
   acolo. `-Shot=0` / `-EnemyShot=0` fac magazia fara fund la loc.

10. **PASA ADVERSARIALA peste tot arcul** (16.09): 6 lentile independente, 49
   de constatari brute, 16 verificate de agenti pusi sa le RESPINGA, 10
   confirmate + 4 de la un critic de completitudine. Dupa unirea duplicatelor:
   11 defecte, toate reparate in doua commit-uri.

   **Ce a gasit, pe categorii** (merita citit inainte de urmatoarea felie,
   fiindca sunt tiparele acestui proiect):
   - instrumente care nu pot iesi rosii: o cheie pe care niciun cod n-o putea
     misca, o ramura pe care niciun scenariu n-o atingea, TREI scenarii
     identice bit cu bit si un al patrulea aproape-duplicat;
   - contoare cu doua meserii: un total de rulare ATRIBUIT din contorul privat
     al unei nave, morti dintr-o echipa de prada intorsi vii la chei, oameni
     cumparati pentru locuri care nu erau goale;
   - stari fara iesire: o prada ajunsa in rada care navigheaza la infinit, un
     negustor ajuns in port care poate inca sa coboare pavilionul;
   - GRANITA DINTRE FELII, punctul orb numit de critic: negustorul mostenea
     opt guri de tun pe o coca fara tunuri, si `-Port=` se citea dupa iesirea
     devreme pentru convoi.

   **Verdictul criticului, de tinut minte:** „arcul e neobisnuit de solid acolo
   unde autorul se uita si neobisnuit de orb acolo unde nu se uita; ce nu are e
   vreo verificare pe granita DINTRE felii."

11. **DARELE GHIULELELOR** (cerute 16.09, cizelate 18.09): o PANGLICA pe
   `UProceduralMeshComponent`, construita din chiar drumul ghiulelei, deci
   curbura e mostenita nu calculata. Conica: ~70 cm la ghiulea, ~520 cm in
   coada. Un lant pe ghiulea. Se inchide unde cade ghiulea, nu cu trei metri
   inainte. Pe TOATE ghiulelele.

12. **NAVA RIDICATA PE LINIA EI DE PLUTIRE**: sferele de flotabilitate coborate
   cu 80 cm (originea de la -78,4 la +1,9 cm) si `GGunPortZ` de la 120 la 280.
   Gura de tun de la **19 cm la 297**. Misca TOATA balistica - 86 de cifre in
   baseline, si `prize_hull` da acum doua prize in loc de una.

13. **OCHIREA DE MANA** (ceruta 18.09): mouse-ul roteste bateria, rotita da
   inaltarea, `X` fixeaza perpendicular. Arc de +/-12 grade care REFUZA, nu
   taie. Trei semne pe apa, pe valuri nu la zero. Cale SEPARATA de a AI-ului si
   de harnasament, deci suita nu se misca din cauza ei - probat prin 43/43
   diferente identice inainte si dupa.

14. **CARTEA NAVEI** (25.09, progresia, felia 1): `Saved/Ledger/book.txt`,
   citita O DATA, la prima coca a jucatorului (`ASeaGameMode::FitFromBook`,
   chemat din `AShipPawn::BeginPlay` dupa umplere si inainte de flag-urile de
   test), si scrisa O DATA in `ASeaGameMode::EndPlay` - drumul iesirii unui
   jucator, pe care suita il parcurge prin `Exec quit`. Duce oameni, coca,
   ghiulele si LADA (bani pe uscat; in vistierie doar cand jucatorul e raider si
   exista rada). Nava pierduta = 1780 din lada, la preturile portului. DOAR in
   partide cu `-Port=1` si fara `-Shot=`/`-ShipHullTest=` (recenzia: fara rada
   cartea doar uza nava). Cititor strict (`book=1`...`end=1`, numere de 1-9
   cifre), recuperare din `.tmp`, pagina refuzata pusa deoparte. Rada vinde doar
   partii pungii (`IsPurseSide`), si doctrina AI de port la fel. Suita:
   `-Ledger=0` in PINNED, douasprezece randuri `ledger_*` pe fisiere din
   `Saved/CI/` copiate din `tools/books/`, garda octet-cu-octet (care si
   restaureaza) pe cartea reala.
   Designul a iesit dintr-un panel (3 designuri, 2 judecatori): ce vinde portul
   peste „ca noua" e felia 2 (15); campania respinsa de ambii, fiindca
   cheia ei era o suma a doua chei existente.

15. **PALANCURILE** (26.09, progresia, felia 2): portul vinde reincarcarea. Doua
   trepte, -2 s fiecare (12 -> 10 -> 8), treapta n costa n x 400. Pe NAVA
   (`AShipPawn::TackleTier`, `GetReloadSeconds()` e singurul numar citit si de
   ceasul tunului, si de panou); cartea o duce (`tackle=`), si duce si comanda
   deschisa (`order=`) - asa suita plaseaza comenzi prin fixturi, fara flag.
   Tasta T (gamepad: butonul din dreapta) comuta comanda; una data de la tasta se
   plateste abia dupa 3 s, ca a doua apasare s-o poata retrage si in rada.
   `-ShipToggleTackle=N` e flag de TEST (inchide cartea). Rada serveste comanda
   dupa ghiulele si inaintea oamenilor/cocii, pret intreg sau refuz, iar refuzul
   cade in reparatii in acelasi tic. `PlaceTackleOrder` e singura usa prin care
   se plaseaza o comanda - si de la tasta, si din carte. AI-ul: nimic. Designul: panel 3+2 (firepower / endurance /
   alegerea), ambii judecatori pe reincarcare; braurile de coca au pierdut
   fiindca perechea lor nu vedea capacul.

Urmatorul: de ales cu owner-ul. **Deschise si stiute:**
- **Reincarcarea e pe tun, dar castigul ei e ingust.** `GunReload[2][4]`
  inlocuieste cele doua float-uri pe bord, si magazia plateste per tun. Ce NU e
  probat: doua ceasuri care chiar diverg in timp, fiindca singurul lucru care le
  desincronizeaza azi e o magazie scurta, si dupa salva partiala magazia e goala.
  Se vede abia dupa o reaprovizionare. Daca cineva vrea sa duca asta mai departe,
  acolo e firul - nu in array.
- **Capitanul AI a fost atins O SINGURA data**, la linia de bataie (19.09): nu
  se mai alinieaza dupa consorti care au incetat sa guverneze. Poarta lui de
  tragere e neatinsa. Trage pe poarta veche de 9 grade din centrul
  cocii, nu pe arcul nou pe tun. Verificarea adversariala a aratat ca daca i se
  da regula noua fara un prag minim de tunuri, DESCHIDE fiecare lupta cu un
  singur tun, determinist - fiindca "macar unul poarta" ajunge la 13,9 grade la
  100 m, adica mai LARG decat poarta lui de acum.
- ~~**Capcana salvei partiale.**~~ **INCHISA pe 19.09.** Reincarcarea e pe tun
  (`GunReload[2][4]`), fumul e aprins implicit de pe 17364af, iar panoul arata
  trei stari. Sunetul a venit pe 25.09: patru unde sintetizate, numarate contra
  evenimentelor (`SOUNDLOG TOTAL`, chei `sound_*`).
- ~~`sinking.casualties_max` a cazut la 0~~ **FALSA ALARMA, verificata si
  inchisa.** Acelasi rand arata acum si `broadsides: 0` si `struck: 0`: NIMENI nu
  trage in scenariul ala, care e un test de scufundare deliberata si nu o lupta.
  Inainte se nimerea o salva pana la scuttle-ul de la t=6; cu navele ridicate,
  angajarea se decaleaza si scuttle-ul vine primul. Deplasare de sincronizare, nu
  defect. Victimele sunt masurate in opt scenarii, de la 3 la 34 (`crew_fight`),
  deci cheia ramane bine exercitata.

  Merita retinut CUM am gresit: am citit o cheie in loc de rand. Aceeasi clasa de
  eroare ca verificarea intr-o singura directie de mai devreme in sesiune - o
  bucata din instrument, luata drept instrumentul intreg.
- ~~Fumul de tun e parcat~~ **DEPARCAT 19.09 si PORNIT implicit.** Era
  subexpus, nu intunecat: culoare autorata la 0,78 intr-o scena cu punct alb
  5793 cd/m2. Reautorat in candele. Suita: zero diferente.
- ~~**Rada repara pe oricine e in cerc.**~~ **INCHIS 25.09:** rada vinde doar
  partii pungii, iar doctrina AI de port socoteste punga doar a partii ei;
  randul `ledger_side` exercita ambele gărzi, separat.
- ~~Linia de bataie lasa urmaritorii pe un lider care a incetat sa navigheze~~
  **INCHIS 26.09:** jumatatea „a coborat pavilionul" din 297c8ba, jumatatea „a
  rupt lupta" acum (`IsBreakingOff`, perechea `line_breaks`/`line_formed`).
  Commitul de impachetare n-a fost citit de nicio lentila.
- ~~**Fugara blocheaza victoria.**~~ **INCHIS 26.09:** masurat, fugara (6,42 m/s
  cu vantul in pupa) nu poate fi prinsa pe aceeasi polara; peste `EscapeRangeM`
  (2000 m) de jucator scapa, iese de pe apa, iar `TallySquadron` (o singura
  socoteala, si pentru scufundare) da victoria. Perechea `escape_on`/`escape_off`
  plus doua garzi (`escape_fighting`, `escape_player_down`).
- Doctrinele `-AIPrize=1` si `-AIRefit=1` pot scoate LIDERUL din lupta cu coca
  peste 30%; amandoua stinse implicit si fara rand cu escadra - linia nu se
  strange peste el in cazurile astea.

- **`RangeBias` e o constanta si nu poate fi.** Dupa ce bara de cadere a invatat
  inaltimea gurii de tun, reziduul ei isi schimba SEMNUL pe la sapte grade: +4%
  la doua grade, -0,8% la opt. Asta e semnatura unui model de rezistenta care nu
  depinde de timpul de zbor. Sub un procent la distantele la care se lupta, deci
  nu urgent - dar cifra e acolo si e sistematica, nu zgomot.

## ~~Efectele de tragere - DE FACUT MAI TARZIU~~ TOATE TREI LIVRATE 19.09

Owner-ul, cuvant cu cuvant: "scopul lor [al darelor] este doar sa faca ghiulelele
si traseul lor mai vizibile, ca efect de tragere vom avea un mic fum care se
disipeaza mai greu si imediat la tragere un foc scurt si o sa vreau si feedback
la contact, dar noteaza asta pentru mai tarziu".

**Sectiunea asta a stat aici, cu titlul "DE FACUT MAI TARZIU" si data de azi,
dupa ce toate trei fusesera construite.** O recenzie a prins-o. Conta, fiindca
fisierul asta spune despre el insusi ca exista ca o sesiune noua sa poata
continua fara sa reciteasca un DEVLOG de 4000 de randuri - iar o sesiune care il
citea asa cum scrie era trimisa sa reconstruiasca focul, cu un plan de
implementare DIFERIT de cel livrat.

1. **Fum la gura tunului** - `AGunSmoke`, LIVRAT si aprins implicit din 17364af.
   Nu era o functie noua: era o reautorare de culoare in candele, fiindca
   `CoreColor` 0,055 intr-o scena cu punct alb 5793 cd/m2 e subexpus de trei
   ordine de marime. Masurat cu `smoke_spawned` si perechea `smoke_off`.
2. **Foc scurt la gura tunului** - `AMuzzleFlash`, LIVRAT in 05b5f26. 0,10 s,
   trei cartele billboardate pe `InstancedStaticMeshComponent` (NU pe plasa
   procedurala, cum propunea nota veche), miez la 60 000 cd/m2. `-ShipFlash=0`.
3. **Feedback la contact** - `AHullSplinters`, LIVRAT. Paisprezece aschii de
   stejar aruncate dintr-o cocca lovita. Nu s-au cerut detalii de la owner
   fiindca intrebarea avea un raspuns masurabil: o RATARE arunca o coloana de apa
   vizibila de la trei sute de metri, iar o LOVITURA nu producea nimic - singurul
   rezultat pe care jucatorul il urmareste era cel fara nimic de privit.
   `-ShipSplinters=0`.

Si o observatie a owner-ului care schimba o decizie de proiectare: **scopul darei
e DOAR lizibilitatea**, nu atmosfera. Deci daca vreodata se pune intrebarea "sa
traiasca mai mult ca sa arate mai bine", raspunsul e nu - fumul e cel care face
atmosfera, iar dara face cititul.

**Abordajul ramane AMANAT.**

## Ce aşteaptă ochiul owner-ului

`OWNER_VERIFY.md` are **42** de puncte; **16–36 şi 39–42 n-au fost confirmate niciodată**, 37 e reparat, 38 e o DECIZIE a lui
— sunt judecăţi vizuale pe care nu le pot face eu. (Scria 34 aici, şi numărul a
rămas în urmă de două ori la rând: cele mai NOI puncte sunt exact cele pe care
un cititor al acestei linii nu le-ar fi deschis.) Punctul 20 nu mai e o
întrebare: fumul e pornit implicit din 17364af.

`tasks/REVIEW2_OPEN.md`: recenzia a doua e închisă complet, 34 din 34.
