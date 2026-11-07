#!/usr/bin/env bash
# hunter-apply/answers: the assertions of hunter-skill/tests/test_apply.py, run against the C++ tool, plus rows worked
# out from the BANK patterns by hand. Fixtures: tests/fixtures/hunter-apply/application-form.json and the skill's own
# profile.example.json, unmodified.
source "$(dirname "$0")/lib.sh"
F="$FIX/hunter-apply"; FORM="$F/application-form.json"; EXP="$F/profile.example.json"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
WS="$TMP/hunter"; mkdir -p "$WS"; PIPE="$WS/pipeline.json"; PROF="$WS/profile.json"
cp "$EXP" "$PROF"
reset_pipe() { jq -n '{roles: [{id: "google-swe", company: "Google", title: "Software Engineer", url: "https://www.google.com/about/careers/applications/jobs/results", stage: "saved", applied: null, next_step: null, next_date: null, notes: ""}], contacts: [], threads: []}' > "$PIPE"; }
reset_pipe
BASE=(--profile "$PROF" --pipeline "$PIPE" --workspace "$WS")
A() { run hunter-apply/answers "${BASE[@]}" "$@"; }
j() { echo "$OUT" | jq -c "$1"; }
q() { A answer --question "$@"; }

# ---- facts from the profile
q "First Name"; eq "$(j .answer)" '"Jordan"' "first name"; eq "$CODE" 0 "filled exits 0"
q "Last Name"; eq "$(j .answer)" '"Reyes"' "last name"
q "Email address"; eq "$(j .answer)" '"jordan.reyes@example.com"' "email"
q "Current company"; eq "$(j .answer)" '"Brightline Labs"' "current company"
q "School"; eq "$(j .answer)" '"University of Michigan"' "school"
q "How many years of experience do you have?"; eq "$(j .answer)" '"3"' "years, as a string"
q "Are you legally authorized to work in the United States?" --options Yes No
eq "$(j '[.answer, .status, .source]')" '["Yes","filled","profile.work_authorization"]' "authorized"
q "Will you now or in the future require sponsorship?" --options Yes No; eq "$(j .answer)" '"No"' "sponsorship"
q "Are you willing to relocate?" --options Yes No; eq "$(j .answer)" '"Yes"' "the saved sentence maps onto the option"
q "Are you willing to relocate?"; eq "$(j .answer)" "\"$(jq -r .answers.relocation "$EXP")\"" "without options: the saved sentence"
eq "$(j 'keys_unsorted')" '["label","key","answer","status","source","note","required"]' "row keys"
q "Graduation year"; eq "$(j '[.answer, .key]')" '["2023","education.year"]' "graduation year, as a string"
q "Degree"; eq "$(j .answer)" '"B.S.E. Computer Science"' "degree"
q "Job title"; eq "$(j .answer)" '"Software Engineer"' "current title"
q "Full name"; eq "$(j '[.answer, .key]')" '["Jordan Reyes","name"]' "full name"
q "Location (city)"; eq "$(j .answer)" '"Chicago, Illinois"' "location"
q "GitHub"; eq "$(j .answer)" '"https://github.com/your-handle"' "github"
q "Personal website"; eq "$(j '[.answer, .status, .key]')" '[null,"ask","links.website"]' "no website: ask"
q "When can you start?"; eq "$(j .answer)" '"Two weeks after accepting an offer"' "start date"
q "Are you comfortable working in-office 3 days a week?"; eq "$(j .key)" '"onsite"' "in-office is onsite"
q "How did you hear about us?"; eq "$(j .answer)" '"The company'"'"'s careers site"' "how did you hear"
q $'First\xc2\xa0Name'; eq "$(j '[.key, .answer]')" '["name","Jordan Reyes"]' "a no-break space is not the literal space in 'first name'"
q $'Desired\rsalary'; eq "$(j .key)" '"salary_expectation"' "Python's . crosses \\r"
q $'Desired\nsalary'; eq "$(j .key)" 'null' "but not \\n"
q $'Years\xe2\x80\x83of experience'; eq "$(j .key)" 'null' "an em space is not a literal space either"
q "Pronouns"; eq "$(j '[.status, .note]')" '["skip","optional: leave blank unless the user wants to answer"]' "optional pronouns"

# ---- missing means ask, never guess
q "Phone number"; eq "$(j '[.status, .answer]')" '["ask",null]' "no phone"; eq "$CODE" 1 "ask exits 1"
q "What are your salary expectations?"; eq "$(j .status)" '"ask"' "salary expectation"; has "$(j .note)" "/hunter-salary" "salary note"
q "Desired annual compensation"; eq "$(j .key)" '"salary_expectation"' "desired ... compensation"
q "Do you have a non-compete agreement?"; eq "$(j '[.status, .key]')" '["ask",null]' "unknown question"
eq "$(j .note)" '"not a question the profile answers: ask the user, never guess"' "never guess"
q "Referred by"; eq "$(j .status)" '"ask"' "referral"; has "$(j .note)" "only a real referral" "a real referral"
q "Why are you leaving your current job?"; eq "$(j '[.key, .note]')" '["why_leaving","missing from hunter/profile.json: ask the user, then save the answer there for next time"]' "why leaving"

# ---- sensitive questions are the user's
for l in "Gender" "Race/Ethnicity" "Are you a protected veteran?" "Disability status"; do q "$l"; eq "$(j '[.status, .answer]')" '["sensitive",null]' "sensitive: $l"; done
q "Gender"; eq "$(j .note)" '"voluntary self-identification: the user answers this themselves (or declines)"' "the user answers"
q "What is your current salary?"; eq "$(j .status)" '"sensitive"' "salary history"; has "$(j .note)" "never estimate" "never estimate"
q "Sexual orientation"; eq "$(j .key)" 'null' "\\bsex\\b is a whole word"
q "Hispanic or Latino?"; eq "$(j .key)" '"self_id.race_ethnicity"' "hispanic"
jq '.self_identification.veteran_status = "I am not a protected veteran"' "$EXP" > "$TMP/vet.json"
run hunter-apply/answers --profile "$TMP/vet.json" answer --question "Are you a protected veteran?" --options "I am a protected veteran" "I am not a protected veteran" "I don't wish to answer"
eq "$(j '[.answer, .source, .note]')" '["I am not a protected veteran","profile.self_identification","voluntary: filled with the user'"'"'s own saved choice"]' "the user's own saved choice"
eq "$CODE" 1 "sensitive exits 1"
jq '.self_identification.gender = "Prefer to self-describe"' "$EXP" > "$TMP/g.json"
run hunter-apply/answers --profile "$TMP/g.json" answer --question "Gender" --options Male Female
eq "$(j .answer)" '"Prefer to self-describe"' "no option matches: the saved value"

# ---- role-specific
q "Why do you want to work at Google?" --role google-swe; eq "$(j .status)" '"role-specific"' "why this company"; has "$(j .note)" "/hunter-cover" "write it with /hunter-cover"
mkdir -p "$WS/covers"; echo cover > "$WS/covers/google-swe.md"
q "Cover letter" --role google-swe; eq "$(j .source)" "\"$WS/covers/google-swe.md\"" "the approved cover letter"
eq "$(j .note)" '"use the approved cover letter for this role (or its first paragraph)"' "cover note"
q "Cover letter"; has "$(j .note)" "/hunter-cover" "no role: write one"
jq '.answers.why_this_company = "Your infra team"' "$EXP" > "$TMP/w.json"
run hunter-apply/answers --profile "$TMP/w.json" answer --question "Why us?" ; eq "$(j '[.answer, .source]')" '["Your infra team","profile.answers.why_this_company"]' "a saved why"
q "Resume"; eq "$(j .note)" '"the user picks the file in the browser'"'"'s upload dialog"' "resume without a role"
q $'R\xc3\x89SUM\xc3\x89 upload'; eq "$(j .key)" '"resume"' "the accented resume, any case"
q "CV"; eq "$(j .key)" '"resume"' "CV"
q "CVS pharmacy"; eq "$(j .key)" 'null' "\\bcv\\b"

# ---- options that do not match
q "Do you have a Bachelor's degree?" --options Yes No; eq "$(j .status)" '"ask"' "no matching option"
has "$(j .note)" "matches none of the options" "matches none"
eq "$(j .note)" '"the saved answer '"'"'B.S.E. Computer Science'"'"' matches none of the options ['"'"'Yes'"'"', '"'"'No'"'"']: ask the user"' "the note, Python reprs"
q "Have you ever worked for Google before?" --options "No, never" "Yes, I have"; eq "$(j .answer)" '"No, never"' "No -> No, ..."
q "Have you ever worked for Google before?" --options "Nope" "Yes"; eq "$(j .answer)" '"Nope"' "no prefix match"
q "Are you open to relocation?" --options; eq "$(j '[.answer, .status]')" '["Yes. I'"'"'m open to relocating to the Bay Area, Seattle or New York.","filled"]' "an empty --options is no options"

# ---- fill
A fill --questions "$FORM" --role google-swe --json
eq "$(j .counts)" '{"filled":9,"ask":3,"role-specific":1,"sensitive":3}' "counts, in first-seen order"
eq "$(j .ready)" false "not ready"
eq "$(j '.questions | map(select(.label == "Gender"))[0].answer')" null "Gender left to the user"
eq "$(j '[.questions[] | .status]')" '["filled","filled","filled","ask","filled","filled","filled","filled","ask","filled","role-specific","sensitive","sensitive","sensitive","filled","ask"]' "every row"
eq "$(j '[.questions[] | .required]')" '[false,false,false,false,false,false,true,true,false,false,false,false,false,false,false,false]' "required flags"
eq "$(j '.role_id')" '"google-swe"' "role id"
OUTF="$WS/applications/google-swe.json"
A fill --questions "$FORM" --role google-swe --out "$OUTF"
eq "$CODE" 0 "fill exits 0"; has "$OUT" "Stop before Submit" "stop before submit"; hasnt "$OUT" $'\xe2\x80\x94' "no em dash"
[ -f "$OUTF" ] && ok || bad "the plan is saved"
eq "$(echo "$OUT" | grep "^fill  Are you legally")" "fill  Are you legally authorized to work in the United States Yes" "a label cut at 55"
eq "$(echo "$OUT" | tail -2 | head -1)" "9 filled from the profile, 3 to ask the user, 3 for the user alone, 1 written for this role." "the counts line"
echo '{"label": "x"}' > "$TMP/notlist.json"; A fill --questions "$TMP/notlist.json"; eq "$CODE" 2 "a form that is not a list"
eq "$ERR" "error: $TMP/notlist.json must be a JSON list of questions" "not a list"
A fill --questions "$TMP/none.json"; eq "$ERR" "error: $TMP/none.json must be a JSON list of questions" "no form"

# ---- checklist
A checklist --role google-swe; eq "$CODE" 1 "checklist exits 1"
has "$OUT" "[ ] Resume tailored for this role" "no resume yet"
has "$OUT" "[x] Work authorization answered in the profile" "authorization answered"
has "$OUT" "[ ] Form filled in the browser and saved as a draft, not submitted  (answers.py draft --role google-swe --form-url <url>)" "no draft yet"
has "$OUT" "[x] Cover letter written" "the cover letter from above"
has "$OUT" "[ ] Form answers prepared, nothing left to ask" "3 questions to ask"
has "$OUT" "[ ] Company researched  (/hunter-research writes hunter/companies/google.md)" "the company slug"
mkdir -p "$WS/resumes" "$WS/companies"; echo r > "$WS/resumes/google-swe.md"; echo c > "$WS/companies/google.md"
A checklist --role google-swe; has "$OUT" "[x] Resume tailored for this role" "the resume counts"
jq '.questions |= map(if .status == "ask" then .status = "filled" else . end)' "$OUTF" > "$OUTF.t" && mv "$OUTF.t" "$OUTF"
A checklist --role google-swe; eq "$CODE" 0 "the first five done exits 0"; has "$OUT" "[ ] Warm intro (optional)" "warm intro optional"
jq '.contacts = [{id: "c1", company: "GOOGLE"}] | .threads = [{contact_id: "c1", status: "replied"}]' "$PIPE" > "$PIPE.t" && mv "$PIPE.t" "$PIPE"
A checklist --role google-swe; has "$OUT" "[x] Warm intro (optional): the hiring manager replied or a chat is booked" "a reply from a Google contact"
reset_pipe

# ---- draft, then the user submits
A draft --role google-swe --form-url https://example.com/apply --answers "$OUTF" --today 2026-10-23
eq "$CODE" 0 "draft exits 0"; has "$OUT" "not submitted" "not submitted"
eq "$(jq -c '.roles[0] | [.stage, .application, .next_step, .next_date, .notes]' "$PIPE")" "[\"saved\",{\"status\":\"draft\",\"saved\":\"2026-10-23\",\"form_url\":\"https://example.com/apply\",\"answers\":\"$OUTF\"},\"Review the application draft and submit it yourself\",\"2026-10-23\",\"2026-10-23: Application filled and saved as a draft, not submitted (https://example.com/apply)\"]" "the draft record"
A submitted --role google-swe --confirm "yes"; eq "$CODE" 2 "yes is not a confirmation"
eq "$ERR" "error: record it only after the user says they submitted it themselves (for example \"I submitted it\"); got 'yes'" "the refusal"
A submitted --role google-swe --confirm "go ahead"; eq "$CODE" 2 "go ahead is not either"
A submitted --role google-swe --confirm "Done, I submitted it" --date 2026-10-24
eq "$CODE" 0 "submitted exits 0"; eq "$OUT" "Stage: applied on 2026-10-24. Next: a polite follow-up after 7 days if nothing comes back." "submitted line"
eq "$(jq -c '.roles[0] | [.stage, .applied, .next_date, .application.status, .application.confirmed]' "$PIPE")" '["applied","2026-10-24","2026-10-31","submitted","Done, I submitted it"]' "applied, follow up a week later"
eq "$(jq -r '.roles[0].notes' "$PIPE")" "2026-10-23: Application filled and saved as a draft, not submitted (https://example.com/apply)
2026-10-24: Application submitted by the user (Done, I submitted it)" "dated notes"
A draft --role google-swe --form-url https://example.com/apply --today 2026-10-25; eq "$CODE" 2 "no draft after applying"
eq "$ERR" "error: google-swe is already at stage applied" "already applied"
A draft --role nope --form-url https://example.com/apply --today 2026-10-25; eq "$ERR" "error: no role nope in the pipeline (add it with /hunter-scout pick or /hunter-tracker)" "unknown role"
A checklist --role google-swe; has "$OUT" "[x] The user reviewed and submitted it themselves" "submitted is done"
reset_pipe
A submitted --role google-swe --confirm "I CLICKED SUBMIT" --date 2026-12-28; eq "$(jq -r '.roles[0].next_date' "$PIPE")" 2027-01-04 "across the year"
reset_pipe
A submitted --role google-swe --confirm "resubmitting" --date 2026-10-24; eq "$CODE" 2 "a word inside a word does not count"
A draft --role google-swe --form-url u --today 2026-10-2; eq "$ERR" "error: dates are YYYY-MM-DD" "a short date"
A draft --role google-swe --form-url u --today 2026-13-01; eq "$CODE" 1 "an impossible date is not caught, like Python"; has "$ERR" "month must be in 1..12" "the ValueError"
jq '.roles[0].notes = "old note   "' "$PIPE" > "$PIPE.t" && mv "$PIPE.t" "$PIPE"
A draft --role google-swe --form-url u --today 2026-10-23; eq "$(jq -r '.roles[0].notes' "$PIPE")" "old note
2026-10-23: Application filled and saved as a draft, not submitted (u)" "notes keep earlier lines"

# ---- needs a profile, CLI
run hunter-apply/answers --profile "$TMP/none.json" answer --question Email; eq "$CODE" 2 "no profile"
has "$ERR" "profile.example.json" "points at the example"
A answer --question "Gender"; eq "$CODE" 1 "gender exits 1"; has "$OUT" "sensitive" "sensitive"
run hunter-apply/answers --profile "$TMP/none.json" --pipeline "$PIPE" checklist --role google-swe; eq "$CODE" 1 "checklist works without a profile"
has "$OUT" "[ ] Work authorization answered in the profile" "but authorization is unknown"
A; eq "$CODE" 2 "no command exits 2"; has "$OUT" "usage: answers" "usage"
A answer; eq "$CODE" 2 "--question is required"
done_ "hunter-apply/answers"
