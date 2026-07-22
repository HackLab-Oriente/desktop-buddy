# Buddy Zero's reflexes — upload via the web UI (http://<device-ip>/) and
# edit live: no recompile, no reflash. This file IS the demo.
#
# API: buddy.on(pattern, fn)
#      buddy.face.emotion(neutral|happy|curious|sleepy|surprised|angry|sad)
#      buddy.led.mood(calm|excited|thinking|off) · buddy.say(text) → Brain
#      buddy.show(text) → words on screen · buddy.emit(name, payload) · buddy.log(msg)
# Handlers receive a map: ev['name'], ev['payload']. Never block in a handler.

poke_count = 0

buddy.on("touch.down", def (ev)
  buddy.led.mood("thinking")      # instant feedback on contact
end)

buddy.on("touch.pet", def (ev)
  poke_count = 0                  # petting is forgiveness
  buddy.face.emotion("happy")
  buddy.led.mood("excited")
  buddy.say("The user just petted you gently. You forgive all past pokes.")
end)

buddy.on("touch.poke", def (ev)
  poke_count += 1
  if poke_count >= 3
    buddy.face.emotion("angry")   # sulking, now with proper eyebrows
    buddy.led.mood("off")
    buddy.show("HMPH.")
    buddy.log("hmph. poked " + str(poke_count) + " times")
  else
    buddy.face.emotion("surprised")
    buddy.led.mood("calm")
  end
end)

buddy.on("nfc.tag", def (ev)
  # Tap a fob, read its UID from the serial log, then give it a meaning here:
  if ev['payload'] == "PUT-A-UID-HERE"
    buddy.face.emotion("sleepy")
    buddy.led.mood("calm")
  else
    buddy.face.emotion("curious")
    buddy.say("The user showed you a mysterious card with id " + ev['payload'] + ".")
  end
end)
