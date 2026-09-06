---
target: windscout.emieljanson.com landing page + emieljanson.com/windscout configurator
total_score: 25
max_score: 40
na_heuristics: 
p0_count: 0
p1_count: 4
timestamp: 2026-09-04T08-43-49Z
slug: windscout-emieljanson-com
---
# Windscout launch critique

## Design Health Score

| # | Heuristiek | Score | Kernprobleem |
|---|---|---:|---|
| 1 | Status zichtbaar | 3/4 | Preview en installerstatus zijn sterk; wijzigingen missen soms bevestiging. |
| 2 | Aansluiting op de echte wereld | 2/4 | reTerminal, Best Match, modellen en `kt` vragen voorkennis. |
| 3 | Controle en vrijheid | 3/4 | Goede terugweg in installer; reset en route naar uitleg ontbreken. |
| 4 | Consistentie | 2/4 | Landing en configurator voelen als losse producten en tonen een andere launchstatus. |
| 5 | Fouten voorkomen | 3/4 | Installatie waarschuwt goed; vereisten staan te laat. |
| 6 | Herkennen boven onthouden | 2/4 | Forecastsymbolen en instellingen krijgen weinig uitleg. |
| 7 | Efficiëntie | 2/4 | Goede defaults en live preview; geen presets of reset. |
| 8 | Esthetiek en eenvoud | 3/4 | Mooi en rustig, maar de primaire route is te subtiel. |
| 9 | Herstel na fouten | 3/4 | Installer is sterk; funnel en mobiel missen herstelroutes. |
| 10 | Hulp en documentatie | 2/4 | Geen onboarding, FAQ, databronnen of supportpad. |
| **Totaal** |  | **25/40** | **Acceptable: sterke productcraft, zwakke uitleg en funnel.** |

## Design Specificity Verdict

**De losse pagina's voelen eigen; de totale klantreis nog niet.**

- De configurator is uitgesproken Windscout: live e-ink preview, echt hardwaremodel en sterke USB-installatieflow.
- De landing heeft goede fotografie en een authentiek verhaal.
- Samen missen ze gedeelde navigatie, een herkenbaar merkanker en een expliciete overdracht.
- De detector vond 10 waarschuwingen: 9 font-herhalingen en 1 em-dash-waarschuwing. Eén fontmelding komt uit de spot-review buiten deze journey; de overige meldingen zijn grotendeels stijlclusters, geen tien losse problemen.
- De browsercheck vond geen consolefouten. Het echte probleem is inhoudelijk: nul links van landing naar configurator en tegenstrijdige launchcopy.
- Er is geen betrouwbare zichtbare detector-overlay gemaakt, omdat de browsercontext alleen-lezen was.

## Overall Impression

Windscout oogt al als een echt, zorgvuldig product. De live configurator verkoopt het idee beter dan de landingpage, maar is verborgen. De grootste kans is daarom geen visuele redesign: maak één heldere route van **begrijpen → persoonlijk proberen → hardware kiezen → installeren**.

## Wat werkt

- **De live preview bewijst de waarde.** Je ziet direct hoe Windscout thuis werkt.
- **De interface voelt rustig en premium.** Het fysieke object blijft centraal.
- **De installer bouwt vertrouwen.** Het apparaat draait naar de USB-poort en de volgende stap is helder.

## Priority Issues

### [P1] De funnel is onderbroken

- **Waarom:** de landing heeft alleen `Notify me`; geen link naar de configurator. Andersom ontbreekt context.
- **Fix:** primaire CTA `Try it with your spot`, secundair `What you need`, plus een teruglink `About Windscout` in de configurator. Kies overal dezelfde launchstatus.
- **Suggested command:** `$impeccable shape`

### [P1] De kernbelofte en doelgroep zijn te breed

- **Waarom:** `Catch the next good wind` zegt niet duidelijk voor wie het is en welk gedrag verandert.
- **Fix:** combineer doelgroep, product en uitkomst in één zin. Maak van zes features drie concrete gebruiksuitkomsten.
- **Suggested command:** `$impeccable clarify`

### [P1] De configurator mist een startpunt

- **Waarom:** bezoekers zien een indrukwekkend object, maar geen taak, stappen of uitleg bij Brouwersdam en Best Match.
- **Fix:** compacte header: `Choose your spot · Tune the display · Install`. Toon browser, USB-kabel, apparaat en geschatte tijd vóór installatie.
- **Suggested command:** `$impeccable onboard`

### [P1] Mobiel loopt dood

- **Waarom:** de installer verdwijnt op mobiel; opslaan, delen en verdergaan op desktop ontbreken.
- **Fix:** benoem mobiel als demo en bied `Continue installation on desktop` via link, mail of QR.
- **Suggested command:** `$impeccable adapt`

### [P2] Vertrouwen komt te laat

- **Waarom:** voor hardware van circa $70–$160 en een firmware-installatie ontbreken prijscontext, compatibiliteit, databronnen, privacy en support.
- **Fix:** voeg `Before you build` toe met modellen, prijsrange, benodigde tijd, forecastbronnen, privacy, wat installatie overschrijft en projectstatus.
- **Suggested command:** `$impeccable harden`

## Persona Red Flags

### Jordan — eerste bezoeker

- Begrijpt reTerminal en Best Match niet.
- Kan vanaf de landing niet naar de live demo.
- Weet niet wat Install precies doet of nodig heeft.

### Casey — mobiele bezoeker

- Kan spelen maar niet afronden.
- Krijgt geen duidelijke desktop-overdracht.
- Kan een nieuwe spot niet volwaardig meenemen.

### Sam — bezoeker met toegankelijkheidsbehoeften

- Positief: skip-link, labels, dialogen, focusmanagement, live regions en reduced motion.
- Probleem: settingslabels meten circa **3.03:1 contrast**, onder de richtlijn van 4.5:1 voor kleine tekst.
- De forecast heeft geen simpele legenda.

## Kleine observaties

- Landing noemt E1001/E1002; configurator ondersteunt ook E1003.
- De configurator heeft bij start geen enkele zichtbare heading.
- De landing toont mockdata, maar legt de grafiek niet uit.
- De witte E1002-thumbnail valt weg op de lichte kaart.
- Deel kosten, open-source/repository, updatefrequentie, privacy en support.
- Detectorwaarschuwingen over fonts en em-dashes zijn lage prioriteit.

## Questions to Consider

- Verkoop je primair **rust**, **meer goede sessies**, of **een mooi DIY-object**?
- Richt je je eerst op windsporters die nog hardware moeten kopen, of mensen die al een reTerminal hebben?
- Is de configurator vooral een overtuigende demo, of alleen de laatste installatiestap?
- Zou één geïntegreerde homepage sterker zijn dan twee losse werelden?
