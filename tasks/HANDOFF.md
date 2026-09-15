# Unde e proiectul, pe scurt

Scris ca o sesiune nouă (sau un model nou) să poată continua fără să recitească
DEVLOG-ul de 3000 de rânduri. Starea de la 15.09.2026, commit `e55b562`.

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

## Ce urmează: MECANICA

Grafica e parcată de owner („deocamdată arată ok"). Următorul arc e mecanica de
joc. Ce EXISTĂ deja ca fundaţie: vânt, polară de velatură, avarii pe zone,
reîncărcare, escadron inamic, insule, eşuare, scufundare, reapariţie.

Ce NU există şi e candidat: un motiv să lupţi (obiective/misiuni), abordaj,
echipaj ca resursă, reparaţii în larg, economie/progresie, navigaţie pe hartă.

Direcţia se alege cu owner-ul, nu se presupune.

## Ce aşteaptă ochiul owner-ului

`OWNER_VERIFY.md` are 24 de puncte; **16–24 n-au fost confirmate niciodată** —
sunt judecăţi vizuale pe care nu le pot face eu. Punctul 20 (fumul de tun) e o
întrebare deschisă: merită pornit implicit?

`tasks/REVIEW2_OPEN.md`: recenzia a doua e închisă complet, 34 din 34.
