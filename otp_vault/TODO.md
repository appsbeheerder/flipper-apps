# OTPvault: TODO

Doel: een OTP-kluis voor de Flipper Zero die veiliger is dan de bestaande apps
en compleet voor het gebruik als dagelijkse authenticator.

"Veiliger dan alle andere" is niet te bewijzen. Het meetbare doel is: een
beschreven bedreigingsmodel (A) plus een onafhankelijke review van de
crypto-code.

Status nu: v1.02. Opslag met wachtwoord, PBKDF2-HMAC-SHA256, enclave-slot 11
en AES-256-GCM; TOTP (SHA-1/SHA-256); typen via USB; zelftest met blokkerend
foutscherm; talen en/nl; import via `import.txt`.

## A. Veiliger

- [ ] Foutpogingen en wachttijd bewaren buiten het geheugen (versleuteld en
      aan de enclave gebonden), zodat herstarten de teller niet wist.
      Optioneel wissen na N pogingen.
- [ ] Wachtwoord tot 32 tekens; keuze voor de rekentijd van de sleutelafleiding
      (1,5 / 3 / 5 s).
- [ ] Wachtwoord wijzigen.
- [ ] Herstel bij verlies of kapotte Flipper: de kluis is aan dit apparaat
      gebonden. Optionele versleutelde back-up, alleen met een sterke zin,
      bewust zonder apparaatbinding.
- [ ] Atomisch opslaan met `.bak` en een leescontrole na het schrijven.
- [ ] Bedreigingsmodel in tekst: wat wel en niet beschermd is, eerlijk over
      een aangepaste app op dezelfde Flipper.
- [ ] Onafhankelijke review van de eigen SHA/HMAC/PBKDF2/TOTP-code.
- [ ] Geheugen opruimen na het typen en bij het sluiten.

## B. Compleet voor het doel

- [ ] Accounts toevoegen, hernoemen, verwijderen en ordenen op de Flipper.
- [ ] Meer dan 16 accounts (variabele recordlengte).
- [ ] SHA-512, HOTP en Steam Guard.
- [ ] Import van `otpauth://`-regels en het Google Authenticator-migratieformaat.
- [ ] Telefoonkoppeling: QR op de Flipper, X25519, versleuteld over BLE
      (serieel profiel); later NFC voor Android. Flutter-app op de pc
      bouwen.
- [ ] Typen: optionele Enter, vertraging, BLE-toetsenbord.
- [ ] Tijd: zomertijd automatisch; klokcontrole via de telefoon.
- [ ] Meer talen (één `files/lang_<code>.h` per taal).

## C. Controle en kwaliteit

- [ ] Meer testvectoren in de zelftest: SHA-512, SHA-meerblokken, PBKDF2 met
      4096 rondes, X25519 (RFC 7748), AES-GCM (NIST), en een controle dat de
      enclave-sleutel vast blijft.
- [ ] C-compiler op de pc (MSYS2) zodat de crypto ook buiten de Flipper
      getest kan worden.
- [ ] README, handleiding en de webpagina (`docs/index.html`) bijwerken.
