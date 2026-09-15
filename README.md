# PirateSeas

Prototip Unreal Engine 5.7: navighezi o navă de pirați cu vele pe ocean
deschis și te lupți cu o navă inamică condusă de AI. Plutire fizică, vânt,
tunuri cu balistică reală, integritate de cocă. Totul în C++, nimic în noduri
Blueprint.

Tot ce e aici a fost generat prin script, fără să deschid editorul: nava e
modelată procedural în Blender, iar proiectul Unreal e construit prin API-ul
Python al engine-ului.

## Cum îl deschizi

Dublu-click pe `PirateSeas.uproject`. La prima deschidere Unreal compilează
shaderele pentru plugin-ul Water, ceea ce durează câteva minute. Nivelul de
start este deja setat, deci se deschide direct pe ocean.

Apoi apeși **Play**.

## Comenzi

| Tastă | Acțiune |
|---|---|
| W / S | întinzi / strângi pânza |
| A / D | cârma la tribord / babord |
| Săgeți stânga/dreapta | cârma |
| Q | salvă la babord |
| E | salvă la tribord |
| Shift stânga (ținut) | ochești în greement, nu în cocă |
| Mouse | rotești doar camera, nu nava |

**Nu ai accelerație.** W nu împinge nava, ci întinde pânza. Viteza iese din trei
lucruri: câtă pânză e sus, cât de tare bate vântul și sub ce unghi îl prinzi.

Cârma nu face nimic dacă nava stă pe loc. Autoritatea ei crește cu viteza,
fiindcă are nevoie de apă care curge pe lângă ea.

## Tunurile

Patru tunuri pe fiecare bord. Tragi cu Q la babord și cu E la tribord, iar
fiecare bord se reîncarcă separat, în 12 secunde.

Ghiulelele sunt corpuri fizice reale, cu masă de 15 kg și gravitație. Traiectoria
nu e scriptată, iese din simulare.

Tunurile **ochesc**. Q și E aleg singure cea mai apropiată navă de pe bordul
respectiv, echipajele reglează ridicarea țevii pentru distanța ei și rotesc
tunul spre ea cu până la 12°, cât permitea un afet. Fiecare gură de foc are
împrăștiere proprie, mai mare în rotire decât în ridicare, deci o salvă
încadrează ținta în loc să lovească toate în același punct. Măsurat într-o
luptă de 300 s: 14 salve, 28 de lovituri, ratările scurte sau lungi, nu
laterale.

Nava primește recul: fiecare tun împinge coca în sens opus, aplicat chiar în
dreptul gurii de foc, deci o salvă o și înclină puțin.

## Balistica

**Gravitaţia, da.** Ghiuleaua e un corp rigid care simulează, cu
`SetEnableGravity(true)`, iar solverul de elevaţie are `g = 980 cm/s²` scris
explicit. Verificat prin măsurătoare, nu citit din flag: la elevaţie 4,80° şi
gura tunului la 1,24 m, ballistica prezice 2,585 s de zbor, iar logul dă
2,52–2,58 s pe cele patru ghiulele ale salvei. Viteza orizontală iese 145 m/s şi
la 148 m şi la 365 m, deci **nu există rezistenţă a aerului** — un lucru care se
poate adăuga, dar acum nu e acolo.

**Viteza şi direcţia navei, de pe 13.09 — da.** Până atunci, nu: ghiuleaua
pleca cu `Aim * MuzzleSpeed` şi atât, iar tunurile erau ochite pe unde se afla
ţinta **atunci**, nu pe unde avea să fie. La 145 m/s şi 2,5 secunde de zbor la
bătaie lungă, o navă care face 6,5 m/s se mută şaisprezece metri — mai mult de
jumătate din lungimea ei, şi de-a curmezişul liniei de tir, fiindcă tunurile
trag pe travers.

Cele două merg împreună şi asta e partea măsurabilă. Anticiparea se calculează
pe viteza **relativă**: în sistemul navei care trage, ghiuleaua pleacă cu viteza
de la gură iar ţinta derivă cu (ţintă − propriu), deci aia e singura viteză
pentru care trebuie ochit înainte. Două nave care merg alături cu aceeaşi viteză
nu cer nicio anticipare, iar forma relativă spune asta fără niciun caz special.

Măsurat pe patru seminţe, 150 s, două nave inamice:

| variantă | salve | lovituri | lovituri/salvă |
|---|---|---|---|
| vechi (nici una) | 28 | 33 | 1,18 |
| doar moştenirea vitezei | 28 | **5** | 0,18 |
| doar anticiparea | 28 | 16 | 0,57 |
| **ambele (implicit)** | 28 | 32 | **1,14** |

Fiecare jumătate singură e dezastruoasă, şi asta e dovada că perechea face o
treabă reală în loc să nu facă nimic: moştenirea fără anticipare mută ghiuleaua
lateral cu tot atât cât se mută nava, iar anticiparea fără moştenire
supra-corectează cu exact aceeaşi cantitate. Împreună dau aceeaşi precizie ca
modelul vechi — **nu e un câştig de precizie, e o corectare de fizică**, şi se
plăteşte singură în situaţiile pe care modelul vechi nu le putea reprezenta
deloc.

`-ShipInheritVel=0` şi `-ShipLead=0` readuc exact comportamentul vechi, ceea ce
e tot ce face schimbarea măsurabilă în loc de doar argumentată.

**Despre `RangeBias`.** Tunurile trag cu 4% lung, şi bănuiala a fost că numărul
compensa tocmai anticiparea care lipsea. Baleiat de la 0,80 la 1,40 cu fizica
pornită: media loviturilor se mută într-adevăr, de la −140 m la −105 m, deci
parametrul funcţionează. Dar între 0,98 şi 1,06 diferenţa e **o singură
lovitură din treizeci şi trei**, pe patru seminţe. Aia e împrăştiere, nu semnal,
şi nu se calibrează nimic pe ea. Rămâne 1,04.

## Unde lovești contează

O ghiulea nu mai scade pur și simplu un număr. Contează unde intră.

| Zona | Cum o atingi | Ce costă |
|---|---|---|
| cocă | tir jos, pe bordaj | 60 din 1000; la zero se scufundă |
| greement | tir înalt, în catarge și vele | pânza; cu tot greementul dus, nava e moartă în apă |
| cârmă | doar printr-un tir prin pupă, aproape de axă | cârma; fără ea se rotește de cinci ori mai încet |
| baterie | printr-un sabord, la înălțimea punții tunurilor | un tun; bordul acela trage cu o ghiulea mai puțin |

Măsurat, la vânt bun:

| avarie | efect |
|---|---|
| greement 100% → 60% → 30% → 0 | 6,65 → 5,92 → 4,52 → **0,01 m/s** |
| cârmă 100% → 50% → 0 | 5,45 → 3,25 → **1,17 °/s** |
| 2 tunuri scoase pe bord | salva trage 2 ghiulele în loc de 4 |

**Tirul înalt nu scufundă pe nimeni.** Îi ia mersul. Tirul jos o scufundă, dar o
lasă să fugă până atunci. Asta e alegerea, și e aceeași pe care o făceau
englezii (jos, să spargă coca) și francezii (sus, să taie greementul).

O salvă atinge aproape întotdeauna mai multe zone deodată, și așa trebuie: patru
ghiulele împrăștiate pe 1,6° acoperă la 300 m vreo 17 metri, mai mult decât
jumătate de navă. Ordinul „sus" sau „jos" înclină balanța, nu comută între două
rezultate curate.

Ține **Shift stânga** apăsat când apeși Q sau E și echipajele ochesc în greement.

Inamicul urmează o doctrină pe care o poți prezice: **trage sus până îi rămâi cu
sub jumătate din pânză, apoi trage jos**. Întâi te schilodește, ca să nu poți
alege distanța și nici fugi, apoi te scufundă.

Dacă ai ambele tunuri de pe un bord scoase, salva pe bordul ăla nu mai pornește
deloc și nu-ți consumă nici reîncărcarea.

## Uscatul

Sunt insule pe mare, cu `-Islands=1`. O insulă are o plajă la 110 m de centru,
un deal de 42 m deasupra apei și un banc de nisip care se întinde încă 50 m în
larg de plajă.

**Nava nu atinge niciodată stânca.** Nu e o alegere de gust, e concluzia unei
măsurători: cu uscat care blochează coca, un corp de 60 de tone împins în el e
rezolvat de motorul de fizică singurul fel în care poate fi rezolvată o
pătrundere — în sus, pe fața de deasupra. 302 contacte, normala impactului 1,00,
navele urcate la 25 m și rămase acolo cu 0,00 m/s până la sfârșitul rulării. Sus
n-are niciun pontoon în apă, deci chila, velele și cârma se sting toate în
aceeași clipă și nimic din proiect n-o mai poate mișca vreodată.

Deci eșuezi pe **banc**: apa mică din jurul plajei te împinge înapoi în larg, cu
atât mai tare cu cât stai mai adânc. Trei lucruri de știut despre banc:

- **Împinge doar spre larg.** Nicio valoare a lui nu poate ține o navă care
  pleacă: îți poate lua viteza, altceva nu.
- **Se întărește mult spre mal** — de cincizeci de ori spre ultimul metru, apoi
  plafonat. *Nu* la infinit: e un banc destul de tare, măsurat, nu unul imposibil
  de trecut prin construcție. Diferența contează, și e spusă aici fiindcă prima
  versiune a acestei fraze promitea garanția pe care codul nu o dă.
- **Te măsoară pe toată lungimea, nu într-un punct.** Banca se citește în trei
  puncte — prova, centrul, pupa. Cu un singur punct la mijloc, o navă de 31 m
  tratează un banc de 50 m ca pe unul de 34, iar cei 16 m lipsă sunt exact acolo
  unde e prova: măsurat înainte de asta, ținută în banc cu toate pânzele sus, se
  oprea cu etrava la doi metri și jumătate dincolo de țărm, cu toate citirile în
  verde.
- **Te costă odată, la atingere**, pe cât de repede **intri în banc** — nu pe cât
  de repede mergi. O navă care aleargă de-a lungul coastei și doar atinge marginea
  era taxată pentru toată viteza ei, din care aproape toată era paralelă cu malul.
  O atingere lină e aproape gratis; intratul cu toate pânzele sus te costă un
  sfert din cocă și te poate scufunda acolo unde stai, prin exact aceeași inundare
  ca o ghiulea.

Cum ieși: **strângi pânza.** E singura manetă a unei nave cu vele pătrate și e
și răspunsul corect istoric — n-o împingi mai tare pe uscat.

Măsurat, intrând cu toate pânzele sus, cu 6,56 m/s spre mal: rană de cocă
1000 → 705, adâncimea maximă **la provă** 37 m din 50 disponibili (deci etrava s-a
oprit la 13 m de plajă), viteză la ieșire 0,06 m/s — un arc fără amortizor te-ar
fi aruncat afară mai repede decât ai intrat — și 30 de secunde de la atingere
până e iar liberă. Pescajul nu se schimbă o clipă: `inWater=1` și portanța în jur
de 1,00 pe toată durata.

O insulă așezată peste un punct de apariție e **refuzată**, nu doar semnalată: o
cocă născută în banc e azvârlită afară cu 30 m/s, și un avertisment într-un log
de 2700 de rânduri e un lucru pe care nu-l citește nimeni înainte să creadă
cifrele de sub el.

**Stânca oprește ghiulelele.** Măsurat pe o pereche de rulări identice, cu
aceeași sămânță: fără insulă 8 ghiulele cad în apă, cu insulă 5 cad în apă și
**exact 3 se opresc în stâncă** — aritmetica se închide — iar loviturile în
greement rămân 4 și 4. Uscatul e adăpost, și e un adăpost care se numără în log
(`SHOTLOG land`), nu unul dedus dintr-o scădere a loviturilor.

### Cum schimbă bordul

**Când are viteză, vine în vânt. Când n-are, virează prin pupă.**

Asta a fost, până acum, singura parte a navigației pe care căpitanul o făcea
categoric prost. Regula veche spunea „dacă drumul cel scurt taie vântul, virează
prin pupă" — fără nicio condiție de viteză, deși propriul ei comentariu explica
de ce: *o navă cu vele pătrate trece prin ochiul vântului doar cu viteză
adevărată.* Deci vira prin pupă și la șase noduri, când ar fi putut veni în vânt
în câteva secunde.

Măsurat, bordeind 600 m până la o țintă aflată în vânt: câștiga vreo 40 m pe
fiecare bord și pierdea vreo 110 m la fiecare viraj prin pupă. **În patru sute de
secunde termina cu 285 m mai DEPARTE decât plecase, și nu trăgea niciun foc.**

| | virează mereu prin pupă | vine în vânt când poate |
|---|---|---|
| lupta standard, salve | 3 | **7** |
| aceeași, lovituri | 4 | **13** |
| escadron de două, salve | 5 | **6** |
| bordei de 600 m, salve | **0** | **3** |
| aceeași, distanța la final | 885 m | **380 m** |

**Prețul, spus pe față:** cea mai lungă perioadă prinsă cu prova în vânt crește
de la vreo 6 secunde la vreo 40. Uneori venirea în vânt eșuează și trebuie
plătită. Merită: un viraj prin pupă costă o sută de metri sub vânt de **fiecare
dată**, iar o venire ratată costă patruzeci de secunde **o dată**.

Pragul e 4 m/s, iar treapta e ascuțită fiindcă viteza ei în bordei e 4,89: peste
5 testul nu trece niciodată, sub 4,5 trece mereu. `-AITackAbove=N` îl schimbă, și
o valoare foarte mare readuce exact comportamentul vechi — de asta se poate
măsura în loc să se discute.

### Căpitanul inamic și uscatul

AI-ul **nu ocolește** uscatul: n-are privire înainte, n-are rută, și intră în
banc dacă drumul spre tine trece prin el. Ce face e un singur lucru, și e
lucrul care contează: **odată eșuată, nu se mai mână singură pe uscat.** Strânge
din pânză cât timp încă înaintează spre mal, apoi o pune la loc și cârmește spre
larg pe cel mai bun curs pe care rigul chiar îl poate ține — adică bordeie, dacă
largul e în vânt.

De ce doar atât: măsurat, ce o ținea pe uscat era **pânza**. Comanda „toate
pânzele sus" rula în fiecare cadru fără nicio idee că sub ea e nisip, iar bancul
o împingea afară în timp ce ea se mâna înapoi înăuntru — se vede în log, oscila
între 28 și 38 de metri în banc timp de șase minute.

Măsurat, pe un mal sub vânt cu insulă de 180 m rază:

| | înainte | după |
|---|---|---|
| secunde eșuată (din 400) | 190 | **42** |
| salve trase în toată rularea | **0** (nu ajungea la luptă) | 14 |
| aceleași, cu greementul la 0,45 | 380 s eșuată | **66 s**, 16 salve |
| insulă implicită, pe drum | o atingere, liberă în 14,8 s | o atingere, liberă în **9,6 s** |
| fără insulă în lume | — | log **identic bit cu bit**, `landTicks=0` |

**Prețul, spus pe față:** atinge uscatul mai des decât înainte (de două ori în
prima rulare, de cinci ori cu greementul rupt), fiindcă după ce se dezlipește
ținta e tot dincolo de insulă și ea o ia iar într-acolo. Pierde deci mai multă
cocă pe uscat decât în măsurătoarea veche — dar în măsurătoarea veche nu era în
siguranță, era **blocată**. Privirea înainte, care ar opri atingerile repetate,
e exclusă deliberat din felia asta.

**Poate să ocolească, dar implicit nu o face.** Privirea înainte e construită și
măsurată — `-AILookAhead=90` o pornește — și verdictul măsurătorii e că nu merită
pornită. Pe o coastă de trei insule înjumătățește eșuările și nu costă nimic
(16 salve față de 13, jucătorul ia exact aceeași bătaie). Dar pe cele două
geometrii unde uscatul stă **între ea și țintă** o face inofensivă: pe un mal sub
vânt salvele scad de la 14 la 9 și jucătorul termină cu 760 din cocă în loc de
400, iar pe insula implicită nu-l mai scufundă deloc.

Două geometrii din trei pierd mai multă luptă decât salvează eșuări. Și mai e
ceva ce cifrele nu spun: un inamic care ocolește mereu promontoriul e un inamic
pe care nu-l poți împinge niciodată pe un mal — adică exact lucrul cel mai
interesant pe care uscatul îl poate aduce în jocul ăsta.

Sub vreo cincizeci de secunde de orizont e de-a dreptul dăunătoare: la douăzeci a
eșuat de **opt** ori pe coastă, fiindcă vede uscatul prea târziu și apoi taie
zig-zag.

**Ce tot NU face:** nu se ferește de un mal sub vânt înainte să ajungă pe el, și
**trage în continuare prin insulă**. Ultima e o decizie, nu o
scăpare: testul de „nu trage prin consoartă" se blochează latch-uit, iar pe o
geometrie fixă ca o insulă ar putea amuți un escadron 300 de secunde cu toate
celelalte indicatoare verzi. O ghiulea irosită e mai ieftină decât o luptă
pierdută în tăcere.

**Și o navă care rămâne în urmă se întoarce acum în linie.** Comentariul din
felia escadronului spunea deja că „mai departe de un interval nu mai ține
postul, ci se reîntoarce" — dar reîntoarcerea nu fusese construită, fiindcă
nimic nu arunca o navă destul de departe. Uscatul a aruncat-o: măsurat, o navă
care atinsese bancul termina rularea la 66 km de post, cârmind un curs paralel
cu o linie de care nu mai era nici pe departe. Acum, peste trei intervale,
cârmește spre PUNCTUL de post și pune pânză plină; se întoarce în linie.

## Escadronul

Nu te așteaptă o navă, ci două. `-EnemyCount=N` schimbă numărul, până la opt.

Se formează în **șir**, fiecare ținând locul în urma celei dinainte, la vreo
120 m. Nu e o decorație: tunurile trag pe travers, deci într-o linie de front
consoarta ți-ar sta fix în bătaie. Măsurat cu trei nave în linie de front, una a
pus o ghiulea în alta în primele două secunde. În șir, consoartele stau la 90°
de unde pot trage tunurile. De asta a existat linia de bătaie.

Doar nava din frunte navighează: alege tactica, distanța, bordul. Celelalte îi
țin locul, cu cârma pe cursul celei dinainte și cu **pânza** pe distanță,
fiindcă o navă cu vele pătrate n-are altă manetă. Când cea din frunte se
scufundă, următoarea preia conducerea.

Două reguli care le țin în viață una lângă alta:

- **Nu trag prin consoartă.** Dacă o navă de-a lor e în bătaia tunului, bordul
  acela tace până se eliberează culoarul.
- **Se feresc.** O navă care vine prea aproape de o consoartă pune cârma în
  partea opusă, cu atât mai tare cu cât e mai aproape.

Măsurat cu trei nave, 300 de secunde: **zero ciocniri** (erau șase, una cu impuls
de 8,1e6) și **zero lovituri fratricide** (era una).

Panoul îți spune câte mai sunt pe apă. Victoria se numără pe escadron, nu pe
navă: nu ai învins până nu s-a dus și ultima, iar escadronul următor iese la
mare la opt secunde după aceea.

## Convoiul

Primul motiv să lupţi. `-Convoy=N` pune N negustori pe mare (până la şase), care
ies dintr-un punct (`-ConvoyX= -ConvoyY=`, implicit la 1,9 km spre nord-est) şi
fug pe un drum aşezat la `-ConvoyWindAngle=` grade faţă de vânt spre o radă la
`-ConvoyRangeM=` metri (implicit 1500). Implicit drumul e la 90 de grade, adică
DE-A CURMEZIŞUL vântului, ca „în vântul convoiului" şi „sub vântul lui" să
însemne acelaşi lucru toată partida, nu doar la început. O navă ajunsă la 150 m
de radă e sub tunurile fortului şi a scăpat.

Un negustor nu luptă până la ultima scândură: **coboară pavilionul** când i-a
rămas sub 60% din cocă sau sub 60% din greement. Atunci strânge pânza, pune
cârma la mijloc şi nimeni nu-l mai vânează — dar loviturile tot intră: cine
continuă să tragă într-o navă care a coborât pavilionul o scufundă, şi pierde
exact ce voia.

Convoiul e **LUAT** când ai oprit `-ConvoyNeed=` nave (implicit jumătate,
rotunjit în sus) şi **A TRECUT** când destui au ajuns în radă sau pe fund încât
numărul ăla nu se mai poate atinge. Panoul arată socoteala. Fără prăzi, fără
valoarea mărfii, fără escortă, fără abordaj: astea sunt feliile următoare. Asta
e doar partea care face din **partea vântului pe care stai** decizia — polara
măsurată a proiectului o preţuieşte la vreo 8:1 (500 m sub vânt costă 62 s,
500 m înapoi în vânt costă 481).

Negustorul e aceeaşi cocă, acelaşi greement, acelaşi căpitan AI (care ştie că e
negustor şi navighează în loc să lupte), dar ÎNCĂRCAT: viteza lui maximă e 55%
din a unei nave de luptă — măsurat, 3,6 m/s faţă de 6,5. Nu pânza e butonul: cu
jumătate de pânză tot făcea 5,2 m/s, fiindcă forţa scade cu 1 − v/Vmax şi marea
dă cea mai mare parte înapoi. Linia de plutire e butonul.

`-RaiderSide=weather|lee` şi `-RaiderOffingM=` aşează ESCADRONUL inamic în
vântul convoiului sau sub vântul lui, la atâţia metri de-a lungul vântului.
Raider-ul dintr-o rulare măsurată e o navă inamică obişnuită cu căpitanul
obişnuit, care vânează cea mai apropiată cocă duşmană — iar negustorii îi sunt
duşmani. Nava jucătorului stă unde e. `-ConvoyStrikeTest=N` face primul
negustor să coboare pavilionul la N secunde fără nicio ghiulea, ca drumul de la
pavilion la misiune încheiată să se poată dovedi singur.

### Ce a trebuit reparat la căpitan ca să poată prinde un negustor

Până acum, în raza de angajare, vira să-şi pună tunurile pe ţintă ORICE ar fi
făcut ţinta. Contra unei nave care stă şi luptă e corect; contra uneia care fuge
înseamnă că stă la 490–540 m două sute de secunde fără să tragă un foc — măsurat
cu raider-ul în vântul convoiului, cu 800 m avans. Acum, în afara distanţei de
menţinere şi fără să câştige teren, se duce DREPT spre ea şi pune tunurile când
ajunge. Cu histereză pe DISTANŢĂ, nu pe viteza de apropiere: fugarul deschide
distanţa exact când raider-ul virează să tragă, deci o regulă care relua
urmărirea la 350 m o relua la două cadre după fiecare viraj (293 de rânduri
„running down" într-o rulare, şi o navă care zigzaga în loc să termine vreunul
din drumuri).

Şi îşi păstrează ţinta până iese din luptă. Recăuta la fiecare două secunde —
inofensiv cu o singură cocă duşmană în lume, ruinător cu doi negustori la 150 m
unul de altul: patru ghiulele în greementul unuia, „cel mai apropiat" sare la
celălalt, trei în al lui, şi niciunul destul de rănit ca să coboare pavilionul.

## Nava inamică

Un `AEnemyShipPawn` creat la pornire de `ASeaGameMode`, la 632 m de tine.
Aceeași cocă și aceleași tunuri ca ale tale, doar că la timonă stă
`AShipAIController`.

Are trei tactici:

| Tactică | Când | Ce face |
|---|---|---|
| apropiere | peste 550 m | prova spre tine, toate pânzele sus |
| angajare | sub 550 m | se pune de-a curmezișul ca să-ți prezinte bordul, ținând 320 m |
| retragere | cocă sub 30% | vântul în pupă și fuge |

Nu virează niciodată prin ochiul vântului: dacă drumul scurt până la cursul pe
care îl vrea ar trece prin vânt, pune cârma invers și vine prin pupă, ca o navă
cu vele pătrate adevărată. Fără asta rămânea împotmolită cu prova în vânt câte
optzeci de secunde: măsurat, 5 salve în 300 de secunde în loc de 14.

Partea care contează: **nu se poate întoarce oriunde**. Dacă direcția pe care o
vrea cade în vânt, nu încearcă un curs pe care o navă cu vele pătrate nu-l poate
ține: pune cursul strâns la 70° de vânt, ține mura minimum 45 de secunde, apoi
întoarce pe cealaltă. Adică merge în vânt în zigzag, ca un bric adevărat. E
același model de velatură ca al tău, deci ai aceleași limite ca ea.

Apare la 632 m, aproape în vântul tău. De asta prima apropiere durează câteva
minute: trebuie să facă bordee ca să ajungă la tine.

Când e în rază, alege bordul cu virajul mai scurt, dacă acel curs e navigabil,
și trage când ținta îi intră la 9° de travers, cât pot tunurile să se rotească.

Când o scufunzi, la 8 secunde apare alta, la același loc de pornire.

## Scufundarea

La integritate zero nava nu dispare și nu rămâne o țintă: se scufundă, în
patru faze pe care le vezi.

| Faza | Ce se întâmplă | Cât |
|---|---|---|
| inundare | lovitura fatală decide bordul: pierde portanța pe acel bord, se lasă, se înclină spre spărtură și se afundă cu prova sau pupa | ~12 s |
| punte inundată | stă cu puntea la nivelul apei, înclinarea se accentuează; ghiulelele trec prin ea, nicio navă nu se mai poate lovi de ea | 5 s |
| plonjare | ultima portanță se duce, alunecă sub apă cu ~2,5 m/s | ~12 s |
| epavă | la 30 m adâncime e ștearsă din lume | |

Nava care se scufundă nu mai navighează, nu mai trage și nu mai e țintă pentru
nimeni. Dacă e a ta: **înfrângere**, camera rămâne pe ea, iar după 35 de
secunde primești o navă nouă la punctul de start. Dacă e a inamicului:
**victorie**, iar la 8 secunde apare un inamic nou. Game mode-ul ține scorul.

În editor, consola (`~`) acceptă `Scuttle`: îți scufundă nava pe loc, ca să
vezi secvența fără să aștepți 17 lovituri.

## Verificare automată

Două fluxuri, fiindcă unul singur ar minți.

**`checks`** rulează pe un runner GitHub obișnuit, la fiecare push. Acolo NU
există Unreal — motorul e peste o sută de gigaocteți în spatele unei licenţe —
deci fluxul ăsta nu compilează nimic şi nu măsoară nimic din joc. Verifică ce se
poate verifica fără el: că fiecare script se parsează, că niciun fişier de care
depinde Unreal n-are BOM, că nimic care seamănă a secret n-a intrat în arbore,
că nimic regenerat de motor nu e urmărit de git, şi că texturile se construiesc,
ies identice bit cu bit la două rulări, şi au perioade care divid latura.

Şi mai face un lucru: **îşi demonstrează că poate ieşi roşu.** Un pas strică
intenţionat o textură şi cere verificărilor s-o prindă. O poartă pe care nimeni
n-a văzut-o vreodată picând e o poartă în care nimeni n-are motiv să aibă
încredere.

**`measure`** rulează jocul pe bune, fără interfaţă, şi compară numerele cu o
linie de bază din `tools/measurement_baseline.json`. Are nevoie de motor, deci
merge doar pe un runner propriu, pe o maşină care îl are. Cinci scenarii, toate
cu hazardul fixat: o salvă la distanţă cunoscută, o navă mânată pe plajă, una
care navighează sub cârmă, una în care o navă e sabordată dinadins, şi una cu
şase coci şi trei sloturi de siaj.

Ultimele două nu spun nimic despre tunuri. Există ca `ships_sunk` şi contoarele
de siaj să fie **diferite de zero undeva**: `ships_sunk` citea un şir pe care
jocul nu-l scrie niciodată (`sink=sinking`, când fazele se numesc
`afloat|flooding|foundering|plunging|wreck`), deci era pironit la zero şi poarta
nu putea ieşi roşie orice s-ar fi întâmplat în joc. Un contor care dă zero pe
toate scenariile nu se deosebeşte cu nimic de un contor stricat.

Rulabile şi local, fără GitHub:

```
python tools/ci_checks.py
python tools/ci_measure.py            compară cu linia de bază
python tools/ci_measure.py --record   o rescrie, deliberat
```

**O linie de bază lipsă nu e o diferenţă.** Scriptul spune „NEW, not broken" şi
iese roşu fiindcă n-a putut compara, nu fiindcă ceva s-a stricat — distincţia pe
care un `diff` care eşuează pe absenţă a ratat-o deja o dată în proiectul ăsta.

**Şi un număr care nu mai e măsurat nu e o potrivire.** Comparaţia merge pe
REUNIUNEA cheilor, nu doar pe cele culese acum: `lift_mean`, `wake_live_max` şi
`islands_built` se scriu doar dacă logul le poartă, deci o rulare care nu mai
tipăreşte `lift=` nu oferă `lift_mean` deloc — iar o buclă care umblă doar prin
ce-a cules tocmai acum n-ar ajunge niciodată la el şi ar raporta potrivire
curată pentru un joc care nu mai pluteşte. Se raportează separat de MOVED:
cauza şi remediul sunt altele. Comparaţia stă acum într-o funcţie proprie,
`compare()`, şi `ci_checks.py` o hrăneşte cu fixturi pe o maşină fără motor —
cele două defecte de mai sus n-ar fi putut fi prinse RULÂND poarta, fiindcă
rularea ei era exact ce producea lumina verde.

## Diagnostice din linia de comandă

Toate rulările de măsurare merg fără interfață:

```
UnrealEditor-Cmd.exe PirateSeas.uproject -game -NullRHI -unattended -nosound <flag-uri>
```

**Ca să compari două rulări, ai nevoie de trei flag-uri în plus:**
`-UseFixedTimeStep -FPS=60 -ShipSeed=1`. Fără ele rulările NU se repetă. Măsurat:
două rulări cu exact aceleași flag-uri și exact același cod au dat 5 și apoi 8
lovituri în greement. Cu pas fix, cele două loguri sunt identice bit cu bit
până la prima salvă și divergeau exact acolo — la singurul hazard din proiect,
împrăștierea tunurilor. Fizica și căpitanul erau dintotdeauna repetabile;
tunurile nu erau niciodată. Orice comparație înainte/după făcută pe o singură
pereche de rulări citea împrăștiere și credea că citește semnal.

| Flag | Ce face |
|---|---|
| `-ShipQuitAfter=N` | închide jocul după N secunde (game mode, timpul lumii) |
| `-ShipShotAfter=N` | captură de ecran din joc la N secunde |
| `-ShipShots=a,b,c` | MAI MULTE capturi, la secundele alese, numite după secundă |
| `-ShipShotCam=` | vantaj fix: `beam` `low` `bow` `far` `rig`; fără el camera stă unde nimereşte |
| `-ShipShotName=` | prefixul fişierelor, ca două galerii să stea alături |
| `-ShipShotYaw= -ShipShotPitch= -ShipShotArm= -ShipShotHeight= -ShipShotFov=` | suprascriu vantajul, cadru cu cadru |
| `-ShipFireTest=N` | o salvă la tribord la N secunde |
| `-ShipWindSweep=N` | rotește vântul complet în N secunde, cu toate pânzele sus (diagrama polară) |
| `-ShipSinkTest=N`, `-EnemySinkTest=N` | sabordează nava respectivă la N secunde, prin aceeași cale ca o ghiulea |
| `-ShipSinkSide=port` | spărtura de test la babord (implicit tribord) |
| `-ShipHullTest=N` | nava jucătorului pornește cu N integritate, ca o scufundare prin tir să dureze două salve |
| `-ShipRudderTest=N` | pune toată pânza și cârma la maxim de la N secunde, ca să se citească viteza de giraţie |
| `-ShipRigDamage=x`, `-ShipRudderDamage=x` | nava jucătorului pornește cu zona aia avariată (0 la 1) |
| `-ShipGunsDown=N` | N tunuri scoase pe fiecare bord de la start |
| `-ShipForeRigDamage=x`, `-ShipMainRigDamage=x` | fiecare catarg separat, ca panoul să poată fi verificat că le arată diferit |
| `-EnemyX= -EnemyY= -EnemyYaw=` | unde și cum apare inamicul |
| `-EnemyCount=N` | câte nave inamice, de la 0 la 8 (implicit 2; 0 = nimeni nu te vânează) |
| `-Convoy=N` | N negustori (0 la 6, implicit 0 = fără convoi şi fără misiune) |
| `-ConvoyNeed=N` | câţi trebuie opriţi ca să fie LUAT (implicit jumătate, rotunjit în sus) |
| `-ConvoyX= -ConvoyY=` | de unde iese convoiul (cm; implicit 120000, 150000) |
| `-ConvoyWindAngle=` | drumul lui, în grade faţă de direcţia DIN care bate vântul (implicit 90) |
| `-ConvoyRangeM=` | cât are de fugit până în radă (implicit 1500 m; rada e tăiată în cutia oceanului) |
| `-RaiderSide=weather\|lee` | aşază escadronul inamic în vântul convoiului sau sub el |
| `-RaiderOffingM=` | la câţi metri de convoi, de-a lungul vântului (implicit 800) |
| `-ConvoyStrikeTest=N` | primul negustor coboară pavilionul la N secunde, fără tunuri |
| `-Islands=N` | pune N insule pe mare, 1 la 8 (0 sau lipsă = niciuna) |
| `-IsleX= -IsleY= -IsleRadius=` | unde e și cât de mare (raza plajei, implicit 11000 cm) |
| `-ShipRunAground=N` | din secunda N, mână nava în insulă cu toate pânzele sus, apoi strânge pânza la 10 s după atingere |
| `-ShipSeed=N` | **fixează hazardul**, deci rularea se poate repeta |
| `-ShipInheritVel=0` | ghiuleaua NU mai moşteneşte viteza navei (comportamentul de dinainte de 13.09) |
| `-ShipLead=0` | tunurile nu mai anticipează mişcarea ţintei |
| `-ShipRangeBias=x` | cât de lung trag tunurile (implicit 1,04) |
| `-ShipSmoke=1` | fum de tun (implicit STINS, şi **încă nu arată bine**) |
| `-WindBearing=N` | fixează DIRECȚIA vântului, altfel „mal sub vânt" nu e reproductibil |
| `-WindSpeed=N` | fixează și TĂRIA lui; fără asta două treceri peste același unghi sunt luate pe vreme diferită |
| `-ShipPolar=1` | polarul de regim STABILIZAT: ține cârma pe un cap, așteaptă până nava nu mai schimbă nimic, abia atunci scrie rândul |
| `-ShipPolarStep= -ShipPolarDwell=` | câte grade între rânduri, și cât i se dă să se așeze |
| `-AITackAbove=N` | viteza peste care căpitanul vine în vânt în loc să vireze prin pupă (implicit 4; o valoare uriașă readuce comportamentul vechi) |
| `-AIBeatMargin=N` | cât de departe de zona moartă ține bordeiul (implicit 22, adică 70 de grade) |
| `-AILookAhead=N` | secunde de privire înainte după uscat (implicit 0, adică deloc; 90 e valoarea care merge) |
| `-AILandClearance=N` | câtă apă vrea între drumul ei și banc (implicit 20000 cm) |
| `-Islands=N` | până la 8 insule, așezate DE-A CURMEZIȘUL vântului, deci o coastă |
| `-IsleSpread=` | cât de rare, implicit cât să se atingă bancurile |
| `-EnemyRigDamage= -EnemyRudderDamage=` | pornește inamicul avariat (o navă cu jumătate de pânză nu se poate smulge de pe un mal sub vânt) |

Rănile din eșuare se scriu `GROUNDLOG`, nu `SHOTLOG`: o rană de la fund nu e o
lovitură de tun și n-are ce căuta în socoteala tirului.

**Atenție la `-NullRHI`.** Cu el, panoul nu se desenează ȘI nu-și scrie logul,
fiindcă `DrawHUD` e blocat de motor când nu există randare. Logul `HUDLOG` se
scrie acum din `Tick`, deci merge și fără randare; dar dacă vrei să VEZI panoul,
scoate `-NullRHI` și folosește `-ShipShotAfter=N`.

**Atenție la citare în PowerShell.** Un flag cu zecimale trebuie pus între
ghilimele: `"-ShipRigDamage=0.62"`. Scris fără ghilimele, PowerShell îl
trunchiază la 0 și rularea pare că funcționează, doar cu alt rezultat. Flag-urile
cu numere întregi nu au problema asta.

Panoul scrie și el o linie pe secundă, `HUDLOG`, cu exact valorile pe care le
desenează, ca să se poată verifica cifrele din spatele pixelilor dintr-o rulare
care nu randează nimic.

Logul (`Saved/Logs/PirateSeas.log`) scrie `SHIPLOG` o dată pe secundă pe navă,
`SINKLOG` în plus cât timp se scufundă, `SHOTLOG` pe ghiulea, `AILOG` la 2 s
și `SEALOG` din game mode.

## Instrumentele

Nava are un panou, desenat integral în C++ pe canvas: fără widget-uri, fără
texturi de interfață, fiindcă proiectul se construiește prin script iar Python
nu poate scrie noduri Blueprint.

**Roza din dreapta sus nu arată nordul.** Arată unde e puterea, cu prova mereu
în sus:

| Ce vezi | Ce înseamnă |
|---|---|
| conturul palid | ce ar trage greementul întreg, pe fiecare curs |
| linia plină | ce trage acum, cu cât greement ți-a mai rămas |
| sectorul roșu | vântul în care pânzele flutură, nu poți naviga acolo |
| cele două linii verzi | unghiurile care câștigă cel mai repede teren în vânt |
| săgeata de pe inel | de unde bate vântul |
| linia din centru în sus | prova; devine roșie când ești în vânt |
| linia galbenă mai scurtă | drumul REAL, după derivă |

Diferența dintre contur și linia plină e exact ce ți-au tăiat tunurile.

Jos-stânga, **viteza** și, sub ea, cât teren câștigi sau pierzi **spre vânt**.
Al doilea număr e cel care contează când urci în vânt: poți merge cu șase noduri
și totuși să pierzi teren. Roșu înseamnă că pierzi.

Sub el, **starea navei**: coca, fiecare catarg separat, cârma și câtă pânză e
sus. Dreapta jos, **tunurile**: patru pastile pe bord, stinse când afetul e
distrus, plus bara de reîncărcare. Scrie „POINTED HIGH" cât ții Shift.

Peste tot, când e cazul: **IN IRONS**, **DISMASTED**, **RUDDER GONE**,
**SHE IS GOING DOWN**.

## Chila

Nava are un plan lateral: rezistă mult mai tare la mișcarea în lateral decât la
cea înainte. Fără el aluneca lateral la fel de ușor cum înainta, deriva ajungea
la 23 de grade și **nu putea urca deloc în vânt**, oricât de bine ar fi fost
pusă pe curs.

| unghi față de vânt | viteză | derivă | câștig în vânt |
|---|---|---|---|
| 44 și mai jos | 0,00 m/s | — | 0 (zona moartă) |
| 50 | 0,92 m/s | 6,2 | +0,51 m/s |
| 55 | 2,55 m/s | 5,7 | +1,24 m/s |
| 60 | 3,56 m/s | 6,1 | **+1,43 m/s** |
| 65 | 4,29 m/s | 6,6 | +1,32 m/s |
| 70 | 4,89 m/s | 7,2 | +1,04 m/s |
| 80 | 5,68 m/s | 8,4 | +0,07 m/s |
| 90 | 6,13 m/s | 9,4 | -1,12 m/s |
| 120 | 6,33 m/s | 8,8 | -4,07 m/s |
| **141** | **6,41 m/s** | 7,1 | -5,43 m/s |
| 180 | 6,00 m/s | 0,0 | -6,00 m/s |

**Tabelul ăsta e remăsurat 13.09 și e altul decât cel publicat până acum.** Cel
vechi era o măsurătoare TRANZITORIE: vântul se rotea două grade pe secundă în
timp ce nava încă accelera, iar capul ei era liber, deci unghiul din fiecare
rând se schimba din două motive fără legătură. Dădea 50° → 2,5 m/s acolo unde
nava chiar face 0,92, fiindcă intra în unghiul ăla cu viteza luată de la un curs
mai larg. Vezi `-ShipPolar` mai jos.

Ce s-a confirmat din tabelul vechi: **60 de grade e într-adevăr cel mai bun
unghi de urcare în vânt** (+1,43 m/s, față de +1,39 declarat), iar vârful de
viteză e la ~140 de grade. Ce s-a schimbat: la 50 de grade nava practic nu
merge, iar la 70 câștigă în vânt mult mai mult decât se credea (+1,04, nu +0,69).

Deriva de 6-9 grade e cât făcea o navă cu vele pătrate adevărată. Ca să urci în
vânt, ține prova pe la 60 de grade de el: acolo câștigi cel mai repede, chiar
dacă la 70 mergi mai tare. Peste 80 pierzi teren, oricât de repede ai merge.

**Cifra de +1,43 la 60 de grade e adevărată și totuși NU e unghiul la care
trebuie să bordeiască nimeni.** Măsurat, mutând bordeiul de la 70 la 60 de grade:
inamicul ajunge mai aproape de țintă (363 m față de 380) și trage **zero salve**
în loc de trei. Polarul descrie o navă care merge în linie dreaptă la nesfârșit;
o navă care bordeiază își petrece o parte din viață întorcându-se, iar la 60 de
grade merge prin apă cu 3,56 m/s în loc de 4,89 — deci întoarce mai încet,
cârmește mai prost și ajunge prea târziu ca să mai apuce să pună tunurile pe
bord. Bordeiul rămâne la 70 de grade.

Ce a scos la iveală întrebarea asta e altceva, și e mult mai mare. Vezi mai jos.

Chila se opune și rotirii, nu doar derapajului: cercul de giraţie e de 73 m,
cam două lungimi de navă.

## Cum se comportă velele

Nava e cu vele pătrate, deci se poartă ca un bric adevărat:

| Unghiul vântului | Ce se întâmplă |
|---|---|
| sub 48° | **în vânt**, pânzele flutură, nu faci deloc drum |
| 48° spre 90° | prinde treptat, până la 80% din forță |
| 90° spre 140° | zona bună, forță maximă cu vântul pe pupa-travers |
| 140° spre 180° | vânt din pupă, scade la 78% fiindcă velele din spate acoperă |

Unghiul se măsoară față de direcția **din care** bate vântul. Zero înseamnă că
prova e fix în vânt.

Vântul mai face două lucruri: împinge nava lateral, adică derivă, și o înclină
pe o bordură, proporțional cu cât de mult bate din travers.

Vântul nu e constant. Direcția și tăria oscilează lent, din sinusoide
suprapuse cu perioade care nu se repetă scurt, deci nu simți un ciclu.

## Ce conține proiectul

| Ce | Unde |
|---|---|
| nava, 7074 triunghiuri, 37,5 m, originea pe linia de plutire | `Content/Meshes/SM_PirateShip` |
| material părinte + opt instanțe (cocă, punte, lemn, lemn închis, velă, fier, parâmă, vegetație) | `Content/Materials/` |
| nivelul: ocean 5 km, zonă de apă, manager de plutire, cer, soare, ceață, nori | `Content/Maps/L_OpenSea` |
| suprafața mării: grilă radială de 30 880 de triunghiuri, 6 km | `Content/Meshes/SM_SeaSurface` |
| materialul mării: șase unde Gerstner în deplasare de vârfuri | `Content/Materials/M_Sea` |
| actorul care poartă marea și o ține la pas cu fizica | `Source/PirateSeas/OceanSurface.*` |
| nava, ca pawn fizic: plutire, vele, tunuri, integritate | `Source/PirateSeas/ShipPawn.*` |
| vântul lumii, o singură sursă de adevăr | `Source/PirateSeas/WindSubsystem.*` |
| ghiuleaua, corp fizic cu gravitație | `Source/PirateSeas/CannonBall.*` |
| nava inamică și căpitanul ei | `Source/PirateSeas/EnemyShipPawn.*`, `ShipAIController.*` |
| game mode: îți dă nava și creează inamicul | `Source/PirateSeas/SeaGameMode.*` |

## Cum funcționează nava

Rădăcina pawn-ului e o cutie de coliziune care simulează fizică, nu mesh-ul:
`UBuoyancyComponent` aplică forțele pe root, iar așa catargele și velele nu
intră niciodată în calculul fizic. Șase pontoane sferice pe linia de plutire
țin nava sus, centrul de masă e coborât 180 cm ca să se redreseze singură.

Cârma nu face nimic dacă nava stă pe loc, autoritatea ei crește cu viteza. Cu
pânză sus păstrează totuși un minim, ca un echipaj care bracează vergile: fără
asta o navă prinsă cu prova în vânt n-ar mai putea nici să se miște, nici să
vireze, niciodată.

## Lanțul C++ (funcțional din 10.09.2026)

Proiectul e acum proiect C++. Modulul e la `Source/PirateSeas` și compilează:

```
Build.bat PirateSeasEditor Win64 Development -Project=...\PirateSeas.uproject
Result: Succeeded
```

| Componentă | Versiune folosită |
|---|---|
| Visual Studio | Community 2026 (18.10.0) |
| Toolchain MSVC | 14.44.35228 |
| Windows SDK | 10.0.22621.0 |
| NETFXSDK | 4.8 |

Atenție dacă reinstalezi vreodată: folderul de toolset se numește `14.44.35207`,
dar compilatorul din el e `14.44.35228`. Intervalul interzis de Unreal este
`14.44.0-14.44.35210`, deci pare că îl prinde, dar nu-l prinde. Uită-te la ce
raportează UBT, nu la numele folderului.

## Ora din zi

`-Hour=17.5` mută soarele unde l-ar pune ora aia, şi ia cu el culoarea, puterea,
lumina cerului şi banda de expunere. Fără flag nu rulează nimic din toate astea
şi nivelul îşi păstrează lumina cu care a fost scris — felia e **opt-in**
dinadins: o oră care ar fi mutat pe tăcute aspectul livrat şi fiecare număr din
linia de bază ar fi fost o schimbare de lumină deghizată în funcţionalitate.

Soarele urcă după un sinus între răsărit şi apus şi mătură de la est la vest. Nu
e efemeridă, dar are cele două proprietăţi care contează: lumina rade pe apă la
capetele zilei şi vine din alt cadran la fiecare oră.

**Banda de expunere e ANCORATĂ în punctul măsurat, nu calculată.** Aspectul
livrat e 110.000 lux într-o bandă de 12,5–16 EV, obţinută prin baleiere şi
uitat la capturi; orice altă oră e banda aia mutată cu exact atâţia paşi cu câţi
s-a mutat lumina. O bandă calculată din principii — ce am scris prima dată — a
pus apusul cu două stopuri şi jumătate prea sus şi a transformat un asfinţit
corect colorat într-o siluetă. Aritmetica era bună; ancora era inventată.

## Vântul, în ce se îndoaie

Marea răspundea la vânt de azi-dimineaţă; plantele şi greementul nu răspundeau
deloc. Materialul navei mută acum vârfurile cu vântul — `SwayAmount` pe piesă,
peste `SwayHeightCm` de înălţime proprie — iar C++-ul împinge direcţia, viteza şi
un ceas în materiale dinamice: insula pentru plantele ei, fiecare navă pentru
cele şapte sloturi ale ei.

Forma contează mai mult decât amplitudinea. Planta e **îndoită sub vânt întâi**
şi oscilează în jurul îndoirii; îndoirea creşte cu viteza vântului; o rafală
lentă o umflă şi o lasă; iar deplasarea e scalată cu înălţimea deasupra originii
piesei, LA PĂTRAT, deci rădăcina nu se mişcă şi vârful se mişcă cel mai mult. O
frunză care alunecă lateral din rădăcină nu e vânt, e un mesh care se desface.

Tot greementul se mişcă la fel de mult (`0.10` peste 1800 cm): un sart e legat de
vârful catargului, deci dacă s-ar mişca de patru ori mai mult — ce dădea o citire
„parâmele sunt mai flexibile" — s-ar desprinde vizibil de vergă.

**Amplitudinea e fizică, nu grafică.** Şase centimetri pe metru pe secundă: la
14 m/s, coroana unui palmier de şase metri face vreo optzeci de centimetri. De
la distanţa de la care e privită insula, ăsta e un pixel-doi — şi asta e
corect. O versiune intermediară avea 2,2 metri şi arăta ca un ştergător de
parbriz.

`tools/png_diff.py` a apărut din felia asta: compară o CASETĂ din două capturi şi
dă un procent. L-am scris fiindcă „palmierii se mişcă în vânt" e exact genul de
afirmaţie pe care proiectul ăsta o ia greşit uitându-se la ea — şi primul lucru
pe care l-a spus a fost că nu se mişcă nimic. Avea dreptate: sonda de înălţime
legată la emissive era de 1,0 într-o scenă luminată cu 110.000 de lux, adică o
lumânare la amiază. Ridicată la 120.000 de niţi, a arătat că rampa era 1 peste
toată nava, fiindcă `ObjectBounds` întorcea zero pentru mesh-ul ăla.

## Ce rămâne de construit

- AI care știe de uscat: să nu se lase prins pe un mal sub vânt e REZOLVAT;
  rămâne că trage prin insulă, ceea ce README-ul de mai sus numește o decizie
  deliberată, nu un gol
- umbra de vânt în adăpostul insulei
- pânza nu fâlfâie: vela se îndoaie cu catargul, dar n-are mişcarea ei proprie
- cerul e tot cel implicit al motorului (`-Hour=` mută soarele şi expunerea,
  dar norii şi atmosfera rămân cum sunt), şi timpul nu curge în timpul unei
  partide
- niciun sunet
- coca nu poartă urme de lovitură: gaura se vede în cifre, nu pe lemn
- convoiul n-are prăzi, valoare a mărfii, escortă sau port adevărat (rada e un
  punct pe apă); negustorul nu ştie să vireze prin vânt, deci un drum aşezat în
  vânt l-ar putea prinde în irons
- abordajul e AMÂNAT explicit de owner; echipajul + reparaţiile şi economia +
  progresia sunt următoarele ateliere, în ordinea asta

Reparate de când secțiunea asta a fost scrisă, și scoase din ea ca să nu fie
refăcute: mai multe insule odată (până la opt, cu `-Islands=N`), și garda de
apariție care acum ȘTIE de uscat — fiecare punct în care se poate naște o cocă e
verificat înainte să se pună uscat peste el.

## Stare verificată

Am rulat jocul fără interfață și am făcut capturi. Confirmate vizual de mine:

- nivelul se încarcă, `ASeaGameMode` e game mode-ul activ
- nava se randează, la scara corectă, cu materialele ei: cocă maro închis, vele
  crem, catarge de lemn, tunuri metalice
- cerul, norii volumetrici, atmosfera și ceața se randează
- ambele nave plutesc pe portanță, măsurat: forța de plutire / greutate = 1,00
  în medie, pescaj mediu 74 cm, legănate de o hulă de 1,4 m
- scufundarea, măsurată din log pe ambele borduri: listă spre spărtură, prova
  jos, punte inundată la ~12 s, epavă la 30 s, respawn la 35 s
- polarul de velatură, bătaia tunurilor (398–504 m) și înclinarea de recul sunt
  măsurate din log, nu presupuse
- inamicul se apropie, iese din vânt, se pune de-a curmezișul și deschide focul:
  8 salve, 22 de lovituri în 300 s, iar jucătorul e scufundat de tir real

- apa se vede și taie coca la linia de plutire, cu valuri care se mișcă

## Cum arată, şi cum se judecă asta

O pasă de grafică nu se poate verifica dintr-o linie de log, şi nici dintr-o
singură captură: un cadru de mare în mişcare, luat în clipa în care s-a nimerit
să ajungă rularea, e o anecdotă, nu o măsurătoare. De aceea prima piesă a feliei
n-a fost un pixel, ci **galeria**: `-ShipShots=8,25` face mai multe capturi, la
secunde alese, iar `-ShipShotCam=` aşază braţul camerei pe un vantaj fix şi îl ia
de pe controler. Cu `-UseFixedTimeStep -FPS=60 -ShipSeed=1` lumea e în aceeaşi
stare la secunda 20 în orice rulare, deci singurul lucru care diferă între două
galerii e randarea.

Instrumentul şi-a prins singur primul defect: `-ShipShots=8,25` a raportat
`1 frames requested: 8`, fiindcă `FParse::Value` se opreşte la virgulă. Linia
aceea numărată e tot ce a stat între o galerie de trei cadre şi una de unul.

### Texturile sunt generate, nu descărcate

`Scripts/textures.py` scrie douăzeci şi una de PNG-uri — lemn, pânză, fier, nisip,
rocă, iarbă, două foi de riduri de apă şi o mască de spumă. Nimic nu e luat de
undeva: totul e sintetizat, deci întreg aspectul rămâne reproductibil din sursă
şi nimic din arbore n-are licenţă ataşată.

Toate se îmbină EXACT. Trucul e că zgomotul se construieşte în domeniul
frecvenţei şi se transformă înapoi: un spectru pe frecvenţe întregi are
dimensiunea imaginii ca perioadă prin construcţie, deci cusătura nu poate exista.

### Nava n-are UV-uri

Verificat, nu presupus: `Scripts/ship.py` construieşte coca din secţiuni şi nu
creează niciodată un strat UV, în timp ce `island.py` are unul. O textură pusă
pe un mesh fără UV eşantionează acelaşi texel peste tot şi iese... o culoare
plată, adică exact ce trebuia să înlocuiască.

Deci proiecţie **biplanară în spaţiu local**: lateral `(x, z)` sau `(y, z)`, de
sus `(x, y)`, amestecate după cât de mult priveşte suprafaţa în sus.

**Care plan lateral contează.** `(x, z)` e corect pentru o cocă, a cărei lungime
merge pe X. E GREŞIT pentru o velă: vela e modelată în planul Y-Z, deci X e chiar
burta ei — iar folosind burta drept coordonată de textură se desenează curbele ei
de nivel, adică nişte **inele concentrice**. Inelul acela se vedea pe toate patru
velele din prima captură cu texturi şi l-am pus pe seama umbrei catargului, apoi
pe a cusăturii biplanare, apoi pe a bias-ului de umbră. Trei rulări. Era
coordonata. `SideSwap=1` pe instanţa velei, şi a dispărut. Două eşantioane, nu trei,
şi sunt exact cele două care ies corect — pe bordajul unei coci scândurile merg
de la prova la pupa, şi pe punte la fel. În spaţiu LOCAL, altfel lemnul ar înota
pe cocă în timp ce nava navighează.

### Cordajul

Nava are acum greement: sarturi de la parapet la cruce, scări de frânghie între
ele, straiuri înainte la bompres, pataraţine la pupă, braţe de la capătul verg­ii
şi şcote de la colţurile velelor. Opt sute cincizeci de patrulatere, toate
într-un **singur mesh**, fiindcă trei sute de funii înseamnă trei sute de apeluri
de desenare pentru opt sute de grame de cânepă.

Nu e detaliu — e diferenţa dintre o navă şi o jucărie. Ochiul ştie că nişte
catarge de douăzeci de metri nu pot sta în picioare fără ceva care să le ţină,
chiar dacă nu poate numi ce anume caută.

**Dincolo de 240 de metri cordajul dispare.** Funiile de trei centimetri sunt
sub un pixel la distanţa aia, iar geometria sub-pixel cu contrast mare nu se
mediază — scânteiază. Se stinge prin deplasare de vertex: dincolo de prag,
vârfurile funiilor sunt trase spre originea meshului până când toată funia e un
punct. E per-instanţă (`RopeCollapse`), deci coca, puntea şi velele au o
deplasare identic nulă şi nu pot fi atinse de asta.

Scările se opresc la 56% din sart, adică pe la verga de jos, unde pe o navă
adevărată e platforma. Duse până sus, traversează vela de deasupra şi tot
greementul devine desiş.

### Marea

Şase unde Gerstner, ca înainte. Ce s-a adăugat, în ordinea în care se vede:

- **riduri mărunte** din două foi de normale panoramate cu viteze diferite.
  Asta contează mai mult decât tot restul: între crestele unei hule cu şase unde
  suprafaţa e geometric PLATĂ, deci oglinda era răspunsul fizic corect la
  întrebarea pe care o punea materialul vechi;
- **spumă** pe creste, cu o mască de bule, aspră acolo unde e albă — încetarea
  reflexiei e motivul pentru care spuma se citeşte ca spumă şi nu ca vopsea;
- **împrăştiere**: creasta e subţire şi luminată prin ea, adâncitura nu.

### Siajul

O navă lasă acum o dâră de apă albă în urmă. Până la asta marea nu ştia că
trece cineva prin ea.

**Memoria stă în C++, ca firimituri.** Fiecare navă urmărită scapă un punct la
fiecare nouă metri de drum; materialul desenează un lanţ de capsule prin puncte
consecutive. Asta e tot rostul: firimiturile rămân **unde a fost apa**, deci
când pui cârma, siajul rămâne curbat în urmă. Un siaj calculat din capul navei
ar roti toţi cei optzeci de metri ai lui ca pe un băţ.

Alternativa evidentă e un render target ping-ponguit în fiecare cadru. E mai
capabil şi e compromisul greşit aici: pune starea pe GPU, unde instrumentul
întregului proiect — o rulare fără randare care citeşte linii de log — nu poate
s-o vadă. Aşa, fiecare număr e pe procesor şi se poate tipări.

Trei nave, fiindcă bugetul de parametri e real, iar siajul la care te uiţi e al
tău şi al celui mai apropiat adversar. A patra e **numărată şi scrisă în log**
(`ignored=`), nu aruncată în tăcere.

**Sub 1,2 m/s o navă nu lasă nimic** — o cocă nemişcată nu face apă albă, şi
fără poarta asta o navă oprită scapă toate firimiturile în acelaşi loc şi lanţul
se face punct. Se vede în log: o rulare în care jucătorul stă pe loc scrie
`ShipPawn_0:0` lângă inamici cu 8 firimituri fiecare. Aşa se deosebeşte „n-are
siaj fiindcă nu s-a mişcat" de „n-are siaj fiindcă e stricat".

### Vegetaţia

Doi arbuşti din Blender — un palmier înclinat şi o tufă bolovănoasă —
împrăştiaţi pe insulă la pornire prin trasare de rază, ţinuţi în două meshuri
instanţiate ierarhic: o sută patruzeci de plante sunt două apeluri de desenare.

Geometrie **opacă**, deliberat. Palmierul evident se face din câteva cartonaşe
cu alfa, dar proiectul n-a livrat niciodată un material mascat sau translucid şi
cele două încercări de a introduce unul au costat câte o sesiune fiecare. Frunze
solide sunt mai butucănoase decât cartonaşele, iar ce-i trebuia insulei era o
**siluetă ruptă** pe linia cerului — şi unei siluete nu-i pasă dacă frunza are
marginea decupată.

**Pragurile sunt CITITE din material, nu retastate.** `M_Island` decide
nisip-contra-iarbă din `SandTopCm`/`SandFadeCm` şi pământ-contra-rocă din
`RockSlopeStart`/`RockSlopeFade`; împrăştierea cere activului livrat exact cele
patru numere. O a doua copie a lor în C++ ar fi fost corectă exact până la prima
reglare a oricăruia, iar defectul ar fi fost un palmier stând pe nisip pictat —
vizibil doar într-o captură pe care nimeni n-o face. Logul scrie `4 of 4 numbers
read from the material`, deci o cădere tăcută pe valorile implicite s-ar vedea ca
`0 of 4`.

Respingerile se numără separat (`missed=`, `wrong height=`, `too steep=`),
fiindcă „n-a crescut nimic" are patru cauze şi trebuie deosebite fără o a doua
rulare.

Niciun material nou: plantele se desenează cu masterul navei, care proiectează
biplanar în spaţiu LOCAL — exact ce-i trebuie unui mesh fără UV-uri, şi ele n-au,
din acelaşi motiv pentru care nava n-are.

### Stropii

Ghiuleaua îi spune mării unde a lovit; marea ţine minte o secundă şi jumătate;
materialul desenează un inel de apă albă care se deschide şi se subţiază.

Al treilea efect pe acelaşi mecanism, după surful de la insule şi siaj: starea
în C++, împinsă ca parametri, desenată în material. Motivul e acelaşi de fiecare
dată — fiecare număr rămâne pe procesor, unde o rulare fără interfaţă îl poate
tipări. Logul scrie `splashes=8 live=N seen=N lost=N`, deci o salvă care pierde
jumătate din stropi pe inelul de opt sloturi **o spune**, în loc să pară doar
subţire.

Inelul e rupt de textura de bule BRUTĂ, nu de cea cu prag: pragul de 0,45 există
ca linia de surf să rămână continuă, şi e greşit aici — cu el inelul nu poate fi
găurit, iar patru cercuri perfecte pe apă se citesc ca geometrie, nu ca ghiulele
care cad.

### Gulerul şi braţele

Firimiturile sunt ISTORIE. Alte două lucruri au nevoie de unde e nava chiar în
cadrul ăsta:

**Gulerul** — apă albă în jurul cocii, acolo unde ea împinge marea deoparte
acum. Fără el siajul începea în larg, cu o navă plutind înaintea lui, nelegată.

**Braţele Kelvin** — cele două linii de apă spartă care pleacă din etravă sub un
unghi. Unghiul nu e o alegere: rezultatul lui Kelvin e că tiparul de valuri al
unei coci de deplasament stă într-un unghi de vreo nouăsprezece grade şi jumătate
de fiecare parte a drumului, **la orice viteză**.

Amândouă sunt aşezate pe **drumul** navei, nu pe capul ei: face şase până la nouă
grade de derivă, iar apa se închide în urma locului pe unde a trecut cu adevărat.

### Surful

Marea sparge acum pe uscat. Până la asta, apa era complet neştiutoare că
insulele există: aluneca peste o plajă şi pe un deal fără un fulg de alb, şi ăla
e lucrul care cel mai tare trăda că sunt două obiecte separate care doar se
suprapun.

C++ împinge în material poziţia fiecărei insule şi cele DOUĂ raze ale ei — raza
ţărmului şi marginea bancului — exact cele pe care le citeşte şi forţa de
eşuare, deci apa albă apare fix unde coca ar începe să simtă fundul. Opt insule;
a noua se aruncă **cu o linie în log**, nu tăcut.

Surful e o **bandă cu vârf**, nu o rampă: valurile sparg puţin în AFARA liniei
de ţărm, unde fundul se ridică destul cât să împiedice hula. Prima formă era o
rampă care atingea maximul la ţărm — adică îşi punea albul exact acolo unde
nisipul ascunde deja marea. Se vedea în capturi: fie o spălătură pătată în larg,
fie aproape nimic.

Textura de bule **sparge** surful, nu îl **filtrează**: înmulţită direct, o
gaură din textură făcea o gaură în linia de valuri. Are acum un prag sub care nu
coboară, deci linia rămâne continuă şi textura adaugă froth deasupra.

Pragul spumei **nu e o constantă**. C++ îl împinge în material din suma
amplitudinilor care chiar sunt în joc, ca fracţiune (`FoamCrestFraction`, 0,78).
A fost constantă întâi, şi o constantă nu poate fi corectă de două ori: 34 cm
era o creastă rezonabilă la 11 m/s şi a îngropat tot cadrul în alb la 13 — iar
albul a tras expunerea automată în jos până când nava însăşi s-a înnegrit.
Defectul s-a arătat ca „nava nu e luminată", care nu e locul unde era.

### Lumina

Nivelul avea soare, cer, ceaţă şi nori, şi **niciun volum de post-procesare** —
imaginea era developată de ce se nimerea să fie implicit. Acum: expunere
măsurată pe histogramă, îngrădită între stopuri; o curbă filmică cu vârf şi
umăr; Lumen la calitate ridicată, fiindcă marea E în cea mai mare parte reflexie
şi o reflexie rezolvată în opt raze e o pată — pata era exact reproşul.

**Unităţile sunt reale.** `DefaultEngine.ini` are
`ExtendDefaultLuminanceRange`, deci soarele se dă în lucşi adevăraţi:
**110 000**. Prima trecere a pus 7 — un număr plauzibil împrumutat din
convenţia cealaltă — şi scena a ieşit neagră.

## Fumul de tun — FUNCŢIONEAZĂ, şi tot stins implicit

Mecanismul e întreg şi funcţionează: un `AGunSmoke` per TUN (nu per salvă, ca
patru pufuri să facă un banc de fum de unsprezece metri de-a lungul bordului),
treizeci de cărţi instanţiate, jet cu frecare în primele trei zecimi, ridicare,
umflare, derivă sub vânt cu rampă şi forfecare, rotire per carte, plafon de
patruzeci şi opt de pufuri vii cu tăierea **scrisă în log**. Nelegat de navă:
fumul rămâne în urmă, cum face fumul.

Materialul e primul translucid, neluminat şi instanţiat din proiect. Fiecare
capacitate a fost **sondată înainte** (`Scripts/probe_material_api.py`), fiindcă
niciuna nu mai fusese folosită aici. Iar `blend_mode` şi `shading_model` se
citesc ÎNAPOI şi se asertează: helper-ul `sp()` al proiectului înghite un eşec
într-o linie WARN, deci un material rămas opac ar fi raportat „ok" şi ar fi
randat o placă gri.

**Acum arată a fum:** o vălătucire întunecată la gura tunului, ruptă şi cu
şuviţe, care se subţiază şi derivă sub vânt. Livrat tot STINS, dar nu fiindcă e
stricat — fiindcă fiecare măsurătoare de tir din proiect a fost luată fără el, iar
un implicit pornit ar schimba baza tuturor comparaţiilor viitoare.
`-ShipSmoke=1` îl aprinde. Dacă la privit merită să devină implicit, e o
judecată de ochi: punctul 20 din OWNER_VERIFY.

**Măsurat că nu atinge tirul:** aceeaşi rulare cu fum stins şi cu fum pornit dă
**56 de linii de tir şi 11 lovituri în amândouă**. Fumul îşi trage cele treizeci
de numere aleatoare din fluxul LUI, semănat din indexul loviturii, nu din fluxul
global pe care `-ShipSeed` îl fixează.

### Trei erori de aritmetică şi una de arhitectură

- **Cărţile erau cât norul.** 260–400 cm într-un nor de 300 cm: plăci
  coincidente nu pot forma decât un perete. Acum 30 de cărţi de 175 cm semănate
  printr-o bilă (rădăcină cubică, ca să umple uniform, nu să se înghesuie pe
  margine).
- **Pragul de erodare era peste ce putea atinge zgomotul.** Două câmpuri de
  turbulenţă ÎNMULŢITE au media 0,12; pragul pornea de la 0,36. Alfa era zero
  din primul cadru — fumul era invizibil matematic înainte să fie desenat.
  Sumă ponderată în loc de produs, şi pragul mutat în intervalul real.
- **Alfa aplicată de două ori** în varianta aditivă (o dată de mine în culoare,
  o dată de blend): 0,085 la pătrat înseamnă 0,007 pe carte.
- **Aditivul nu se desena deloc.** Alegerea aditivă era raţionată — treizeci de
  cărţi într-un component instanţiat nu se sortează pe adâncime, iar adunarea e
  comutativă, deci n-are problemă de ordine. Măsurat însă: după repararea
  flagului, fiecare build aditiv a randat NIMIC, iar fiecare build translucid a
  randat cărţile. Argumentul era bun şi motorul nu e de acord cu el; câştigă
  motorul.

### DOUĂ capcane care au invalidat măsurători, nu doar fumul

**1. Materialul era ÎNLOCUIT în tăcere.** Un material trebuie să DECLARE că
poate fi folosit pe meshuri instanţiate. Al meu n-o făcea, iar motorul punea
materialul implicit şi spunea asta o singură dată, într-o linie care nu conţine
cuvintele „failed to compile":

    Material ...MID_M_GunSmoke_0 missing bUsedWithInstancedStaticMeshes=True!
    Default Material will be used in game.

Ore de reglat opacitatea, mărimea cărţilor, numărul lor, sursa de zgomot şi
modul de amestec — pe un material care **nu era niciodată pe ecran**. Căutasem în
log formularea pe care o ştiam de la mare („Failed to compile Material"); asta
are alt text. Acum `gunsmoke_material.py` pune flagul ŞI îl citeşte înapoi.

Ce a găsit-o: o sondă cu **o singură variabilă** — emisiv roşu aprins — care s-a
întors GRI. Un amestec aditiv nu poate desena mai închis decât cerul din spate,
iar ăsta desena mai închis; contradicţia aia a forţat întrebarea „e ăsta măcar
materialul meu?", pe care nimic altceva n-o pusese.

**2. Prima rulare de după o reconstrucţie de material NU arată materialul nou.**
Măsurat, nu bănuit: acelaşi material, trei rulări identice la rând — prima dă un
PNG diferit, a doua şi a treia sunt de acord între ele. Orice comparaţie
înainte/după făcută dintr-o singură rulare imediat după ce scriptul a rescris
materialul citeşte o stare veche. **Rulează de două ori şi compară a doua cu a
doua.**

Ce s-a aflat pe drum, fiindcă a costat şase rulări: eşantionarea din textură
ieşea **exact media texturii**, adică plat. Cinci cauze eliminate una câte una —
împachetarea canalelor, sRGB, compresia în tonuri de gri (sufixul `_M` din
convenţia proiectului aruncă tot ce nu e primul canal), tiling-ul coordonatei,
streamingul. Niciuna. Înlocuit cu **zgomot procedural** — care nu cere UV-uri,
nici setări de import, nici streaming — şi erodarea a pornit imediat. Vina era
în calea texturii; care anume rămâne neştiut.

## Limitări cunoscute

- Suprafața mării e desenată de proiect, nu de plugin-ul Water. Plugin-ul
  raportează totul sănătos (zonă, corp de apă, mesh de informații, materiale,
  vizibilitate) și nu desenează niciun triunghi: aceeași cadră cu
  `r.Water.WaterMesh.EnableRendering` pornit și oprit e identică. Plutirea
  folosește în continuare plugin-ul; doar imaginea e a noastră, din aceleași
  șase unde.
- Stropii sunt un inel de apă albă pe suprafaţă, nu o coloană care sare în sus:
  marea se desfăşoară, nu se ridică.
- Surful e un **inel geometric**: nu ştie din ce parte bate vântul, deci sparge
  la fel de tare şi în adăpostul insulei ca pe partea expusă.
- Ridurile de apă se văd că se repetă în zare, ca un moar.
- Velele au acum formă de velă: capul e legat drept de vergă pe toată lungimea
  lui, poala e LIBERĂ şi acolo se umflă cel mai mult, iar adâncimea e o fracţiune
  din LĂŢIME (11%), nu un număr fix de metri care făcea o velă de nouă metri şi
  una de şapte la fel de adânci.
- Roca nu se arată niciodată pe insulă, fiindcă dealul e prea blând ca să treacă
  de pragul de pantă — măsurat, nu bănuit: împrăştierea vegetaţiei raportează
  `too steep=0`, adică niciun punct din toată insula nu e destul de abrupt.
- Riduri de apă se văd că se repetă în zare, ca o dungă orizontală.
- Navele se lovesc doar cu ghiulele, nu una de alta.
- Cutia de coliziune a oceanului e un corp fizic real (WorldDynamic, blochează
  pawn-ii). Coca răspunde cu Overlap la WorldDynamic tocmai ca să NU stea pe
  ea: până pe 11.09 stătea, iar plutirea ducea doar 43% din greutate. Orice
  geometrie viitoare (insule) pe alt canal decât WorldDynamic.

## Cum a fost construit

Scripturile care generează totul, în ordinea rulării:

1. `ship.py` în Blender: modelează coca prin secțiuni transversale, generează
   punte, parapet, catarge, vele bombate, bompres, cabină și tunuri, apoi
   exportă FBX.
2. `build_game.py` în Unreal: importă FBX-ul, construiește pawn-ul și game
   mode-ul prin `SubobjectDataSubsystem`, creează nivelul cu ocean și cer.

Ambele sunt idempotente și pot fi rulate din nou.
