## Debug Summary

- Probleem: “laatste forecast” gebruikte soms de tijdzone van de spot.
- Oorzaak: dezelfde spot-tijdzone werd gebruikt voor twee verschillende betekenissen.
- Fix: de ophaaltijd gebruikt nu de apparaattijdzone; forecastdagen en -uren blijven spot-lokaal.
- Gedrag: alleen een succesvolle nieuwe forecast vervangt de tijd. Bij een mislukte refresh blijft de vorige succesvolle tijd staan.
- Oude installaties: versie 2/3 valt terug op de spot-tijdzone totdat je opnieuw installeert.
- Confidence: hoog.

## Post-Fix Quality

- Web: 432 tests en production build groen.
- Firmware: 243 hosttests en echte E100x-build groen.
- Releases: 5 manifesttests groen.
- Review: drie restfouten gevonden en opgelost; geen open actiepunten.
- Commit: `0f0dbdd`.
- PR: [#7](https://github.com/emieljanson/windscout/pull/7).

### Coverage

- Correctness, testing, maintainability, API-contract en security gecontroleerd.
- De externe Claude-review kon niet starten door een verouderde CLI-optie; dezelfde adversarial review is lokaal uitgevoerd.
- Eén directe firmware-render-unit-test blijft een niet-blokkerende testgap. De tijdzoneconversie en volledige firmware-build zijn wel gedekt.

### Verdict

Ready to merge.

### Actionable Findings

Geen.
