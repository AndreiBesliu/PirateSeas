# Unde e proiectul, pe scurt

Scris ca o sesiune nouă (sau un model nou) să poată continua fără să recitească
DEVLOG-ul de 3000 de rânduri. Starea de la 15.09.2026, după commit-ul „convoiul" (al doilea din arcul de
mecanică).

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
- **Porţile:**
  - `python tools/ci_checks.py` — fără motor, rulează şi în CI hosted
  - `python tools/ci_measure.py` — 10 scenarii headless vs `tools/measurement_baseline.json`
  - `python tools/ci_measure.py --record` — rescrie linia de bază, DELIBERAT, în
    acelaşi commit care o mişcă
  - `python tools/png_diff.py a.png b.png x0 y0 x1 y1` — compară o casetă
- **Reproductibilitate:** `-UseFixedTimeStep -FPS=60 -ShipSeed=1` plus vântul, pe
  fiecare rulare. Fără toate patru, două rulări identice dau numere diferite.
- **Capturi:** scoate `-NullRHI` şi adaugă `-ShipShots=20,21 -ShipShotCam=beam`.
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
11. **Întinderea rulării nu repară o geometrie greşită.** `prizes_refused` a
   citit 0 la 400 s şi la 500 s; cauza nu era timpul, ci că ţinta rămăsese la
   350 m SUB VÂNT, ceea ce polara proiectului preţuieşte la sute de secunde.
   Refuzul s-a făcut determinist, nu răbdător.
12. **Un număr derivat se scrie cu ingredientele pe aceeaşi linie**
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

Următorul: magazia (muniţie finită), apoi un port unde punga să cumpere ceva.
**Abordajul rămâne AMÂNAT.**

## Ce aşteaptă ochiul owner-ului

`OWNER_VERIFY.md` are 24 de puncte; **16–24 n-au fost confirmate niciodată** —
sunt judecăţi vizuale pe care nu le pot face eu. Punctul 20 (fumul de tun) e o
întrebare deschisă: merită pornit implicit?

`tasks/REVIEW2_OPEN.md`: recenzia a doua e închisă complet, 34 din 34.
