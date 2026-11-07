---
name: bland-ai
description: Real-time AI voice calling, permission dispatch, and SMS phone verification service based on Bland AI (app.bland.ai/dashboard). Allows Astro AI to place voice phone calls to the user to request permission for sensitive actions, trigger in-browser Web Audio phone calls (100% free and forever), and send SMS verification codes.
---

# Bland AI Voice Agent & Permission Calling System

Bland AI empowers Astro AI with an autonomous phone and voice agent: Astro can initiate real-time phone calls or free in-browser audio rings whenever it needs explicit human permission, notification alerts, or high-security authorizations.

## Core Capabilities

1. **Permission Voice Calling (Free & Forever In-Browser + Telephony Gateway)**
   - **In-Browser Web Audio Ring**: Astro rings the user's browser or mobile device with a realistic dual-tone phone ring (440Hz + 480Hz cadence). An incoming call banner pops up with Accept / Decline controls.
   - **Real-Time Voice Dialogue**: Once answered, Astro speaks clearly using Piper neural TTS (English, French, or Arabic) or Web Speech API:
     *"Hello! This is Astro AI. I am about to run a high-risk operation: [Task description]. Do you approve this action? Press 1 or say approve."*
   - **Two-Way Voice & Touch Response**: User can speak ("Approve", "Cancel", "Tell me more") or press keypad digits (1 = Approve, 2 = Deny).
   - **Telephony PSTN Gateway**: Supports connecting Bland AI API (`https://api.bland.ai/v1/calls`) or Twilio to ring real cellular phone numbers.

2. **SMS Verification & Number Binding**
   - User registers phone number (e.g. `+1...`, `+213...`).
   - Astro dispatches a secure 6-digit verification code.
   - The user inputs the code to confirm ownership. Once verified, the phone is bound in `.astro_phone.json` for two-factor permission alerts.

3. **Bland.ai Dashboard Integration**
   - **Live Dispatcher**: Send immediate outbound voice calls with custom pathway prompts.
   - **Pathways**: Visual conversation trees (Greeting -> Intent Classification -> Permission Check -> Branching on User Response).
   - **Call Transcripts & Recordings**: Full logs of all past calls, duration, audio playback, latency metrics, and final resolution.
   - **Voice Selection**: Choose between Piper natural voices (LJSpeech, Siwis, Kareem) or custom neural voice models.

## Available MCP & Skill Tools
- `bland_call_user { phone_number?, task, pathway_id?, voice_id? }`
- `request_voice_permission { action_description, command?, timeout_seconds? }`
- `send_sms_verification { phone_number }`
- `verify_sms_code { code }`
- `get_call_logs { limit? }`
