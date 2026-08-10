# Buddy Zero's reflexes — upload via the web UI (http://<device-ip>/) and
# edit live: no recompile, no reflash. This file IS the demo.
#
# API: buddy.on(pattern, fn)
#      buddy.face.emotion(neutral|happy|curious|sleepy|surprised|angry|sad)
#      buddy.led.mood(calm|excited|thinking|off)
#      buddy.say(text)   → these exact words appear on screen
#      buddy.ask(prompt) → ask the Brain; IT decides what the buddy says
#      buddy.emit(name, payload) · buddy.log(msg)
# Handlers receive a map: ev['name'], ev['payload']. Never block in a handler.

poke_count = 0

buddy.on("touch.down", def (ev)
  buddy.led.mood("thinking")      # instant feedback on contact
end)

buddy.on("touch.pet", def (ev)
  poke_count = 0                  # petting is forgiveness
  buddy.face.emotion("happy")
  buddy.led.mood("excited")
  buddy.ask("The user just petted you gently. You forgive all past pokes.")
end)

buddy.on("touch.poke", def (ev)
  poke_count += 1
  if poke_count >= 3
    buddy.face.emotion("angry")   # sulking, now with proper eyebrows
    buddy.led.mood("calm")        # slow red pulse — brooding, not off
    buddy.say("HMPH.")
    buddy.log("hmph. poked " + str(poke_count) + " times")
  else
    buddy.face.emotion("surprised")
    buddy.led.mood("calm")
  end
end)

buddy.on("nfc.tag", def (ev)
  # Tap a fob, read the UID off the screen, then give it a meaning here:
  if ev['payload'] == "PUT-A-UID-HERE"
    buddy.face.emotion("sleepy")
    buddy.led.mood("calm")
  else
    buddy.face.emotion("happy")
    buddy.say(ev['payload'])   # the UID itself, on screen — no serial cable needed
    # ...and the brain's reaction replaces it a moment later. Both verbs in one
    # handler: say puts YOUR words up, ask lets the buddy find its own.
    buddy.ask("The user showed you an unfamiliar card. React with playful curiosity, one short sentence.")
  end
end)
