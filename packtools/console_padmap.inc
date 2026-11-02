// console_padmap.inc - console-controller/padmap: a C++ port of console-skill/console-controller/padmap.py (Jake Schincariol, MIT).
// Controller mapping for a PS2Recomp port: reads console/controls.json (a layout preset plus overrides) and writes a
// drop-in replacement for ps2xRuntime/src/lib/ps2_pad.cpp (same PSPadBackend::readState, same 32-byte DualShock 2
// report, raylib calls only), with optional input replay (PS2X_INPUT_SCRIPT) and recording (PS2X_INPUT_RECORD).
// Subcommands: init, check, gen, install, restore, presets, table. Messages keep upstream's "padmap.py" wording.
#include <ctime>
#include <map>
#include <set>

namespace packtools::padmap {
using namespace pyrt;
using namespace packtools::console_common;

inline const std::string PROG = "padmap";
struct UsageError : std::runtime_error { using std::runtime_error::runtime_error; };
struct ScriptError : std::runtime_error { using std::runtime_error::runtime_error; };   // upstream's ValueError from parse_script

using Named = std::vector<std::pair<std::string, std::vector<std::string>>>;   // an ordered dict of name -> list
using SMap = std::vector<std::pair<std::string, std::string>>;

inline std::vector<std::string>* nget(Named& n, const std::string& k) {
    for (auto& [a, b] : n)
        if (a == k) return &b;
    return nullptr;
}
inline const std::vector<std::string>& nref(const Named& n, const std::string& k) {
    static const std::vector<std::string> none;
    for (const auto& [a, b] : n)
        if (a == k) return b;
    return none;
}
inline bool has_s(const std::vector<std::string>& v, const std::string& x) { return std::find(v.begin(), v.end(), x) != v.end(); }

// ---------------------------------------------------------------- DS2 facts
inline const std::vector<std::pair<std::string, int>>& ds2_buttons() {
    static const std::vector<std::pair<std::string, int>> B = {
        {"SELECT", 0x0001}, {"L3", 0x0002}, {"R3", 0x0004}, {"START", 0x0008},
        {"UP", 0x0010}, {"RIGHT", 0x0020}, {"DOWN", 0x0040}, {"LEFT", 0x0080},
        {"L2", 0x0100}, {"R2", 0x0200}, {"L1", 0x0400}, {"R1", 0x0800},
        {"TRIANGLE", 0x1000}, {"CIRCLE", 0x2000}, {"CROSS", 0x4000}, {"SQUARE", 0x8000}};
    return B;
}
inline int ds2_mask(const std::string& n) {
    for (const auto& [k, v] : ds2_buttons())
        if (k == n) return v;
    return -1;
}
inline const std::vector<std::string>& ds2_order() {
    static const std::vector<std::string> O = {"UP", "DOWN", "LEFT", "RIGHT", "CROSS", "CIRCLE", "SQUARE", "TRIANGLE",
                                               "L1", "R1", "L2", "R2", "L3", "R3", "START", "SELECT"};
    return O;
}
struct StickDir { std::string name; int stick, axis; double value; };
inline const std::vector<StickDir>& stick_dirs() {
    static const std::vector<StickDir> S = {
        {"LSTICK_UP", 0, 1, -1.0}, {"LSTICK_DOWN", 0, 1, 1.0}, {"LSTICK_LEFT", 0, 0, -1.0}, {"LSTICK_RIGHT", 0, 0, 1.0},
        {"RSTICK_UP", 1, 1, -1.0}, {"RSTICK_DOWN", 1, 1, 1.0}, {"RSTICK_LEFT", 1, 0, -1.0}, {"RSTICK_RIGHT", 1, 0, 1.0}};
    return S;
}
inline bool is_stick(const std::string& n) {
    for (const auto& s : stick_dirs())
        if (s.name == n) return true;
    return false;
}
inline const std::vector<std::string>& raylib_buttons() {
    static const std::vector<std::string> B = {
        "LEFT_FACE_UP", "LEFT_FACE_RIGHT", "LEFT_FACE_DOWN", "LEFT_FACE_LEFT",
        "RIGHT_FACE_UP", "RIGHT_FACE_RIGHT", "RIGHT_FACE_DOWN", "RIGHT_FACE_LEFT",
        "LEFT_TRIGGER_1", "LEFT_TRIGGER_2", "RIGHT_TRIGGER_1", "RIGHT_TRIGGER_2",
        "MIDDLE_LEFT", "MIDDLE", "MIDDLE_RIGHT", "LEFT_THUMB", "RIGHT_THUMB"};
    return B;
}
inline const SMap& trigger_axes() {
    static const SMap T = {{"LEFT_TRIGGER_AXIS", "GAMEPAD_AXIS_LEFT_TRIGGER"}, {"RIGHT_TRIGGER_AXIS", "GAMEPAD_AXIS_RIGHT_TRIGGER"}};
    return T;
}
inline std::string trigger_axis(const std::string& src) {
    for (const auto& [k, v] : trigger_axes())
        if (k == src) return v;
    return "";
}
inline const std::vector<std::string>& gamepad_sources() {
    static const std::vector<std::string> G = [] {
        std::vector<std::string> v = raylib_buttons();
        for (const auto& kv : trigger_axes()) v.push_back(kv.first);
        return v;
    }();
    return G;
}
inline const std::vector<std::string>& raylib_keys() {
    static const std::vector<std::string> K = [] {
        std::vector<std::string> v = {"APOSTROPHE", "COMMA", "MINUS", "PERIOD", "SLASH", "ZERO", "ONE", "TWO", "THREE",
                                      "FOUR", "FIVE", "SIX", "SEVEN", "EIGHT", "NINE", "SEMICOLON", "EQUAL"};
        for (char c = 'A'; c <= 'Z'; ++c) v.push_back(std::string(1, c));
        for (const char* k : {"LEFT_BRACKET", "BACKSLASH", "RIGHT_BRACKET", "GRAVE", "SPACE", "ESCAPE", "ENTER",
                              "TAB", "BACKSPACE", "INSERT", "DELETE", "RIGHT", "LEFT", "DOWN", "UP", "PAGE_UP",
                              "PAGE_DOWN", "HOME", "END", "CAPS_LOCK", "SCROLL_LOCK", "NUM_LOCK", "PRINT_SCREEN", "PAUSE"})
            v.push_back(k);
        for (int i = 1; i <= 12; ++i) v.push_back("F" + std::to_string(i));
        for (const char* k : {"LEFT_SHIFT", "LEFT_CONTROL", "LEFT_ALT", "LEFT_SUPER", "RIGHT_SHIFT",
                              "RIGHT_CONTROL", "RIGHT_ALT", "RIGHT_SUPER", "KB_MENU"})
            v.push_back(k);
        for (int i = 0; i < 10; ++i) v.push_back("KP_" + std::to_string(i));
        for (const char* k : {"KP_DECIMAL", "KP_DIVIDE", "KP_MULTIPLY", "KP_SUBTRACT", "KP_ADD", "KP_ENTER",
                              "KP_EQUAL", "BACK", "MENU", "VOLUME_UP", "VOLUME_DOWN"})
            v.push_back(k);
        return v;
    }();
    return K;
}

inline const Named& positional() {
    static const Named P = {
        {"UP", {"LEFT_FACE_UP"}}, {"DOWN", {"LEFT_FACE_DOWN"}},
        {"LEFT", {"LEFT_FACE_LEFT"}}, {"RIGHT", {"LEFT_FACE_RIGHT"}},
        {"CROSS", {"RIGHT_FACE_DOWN"}}, {"CIRCLE", {"RIGHT_FACE_RIGHT"}},
        {"SQUARE", {"RIGHT_FACE_LEFT"}}, {"TRIANGLE", {"RIGHT_FACE_UP"}},
        {"L1", {"LEFT_TRIGGER_1"}}, {"R1", {"RIGHT_TRIGGER_1"}},
        {"L2", {"LEFT_TRIGGER_2"}}, {"R2", {"RIGHT_TRIGGER_2"}},
        {"L3", {"LEFT_THUMB"}}, {"R3", {"RIGHT_THUMB"}},
        {"START", {"MIDDLE_RIGHT"}}, {"SELECT", {"MIDDLE_LEFT"}}};
    return P;
}
inline const Named& keyboard_default() {
    static const Named K = {
        {"UP", {"UP"}}, {"DOWN", {"DOWN"}}, {"LEFT", {"LEFT"}}, {"RIGHT", {"RIGHT"}},
        {"CROSS", {"X", "SPACE"}}, {"CIRCLE", {"C"}}, {"SQUARE", {"Z"}}, {"TRIANGLE", {"V"}},
        {"L1", {"Q"}}, {"R1", {"E"}}, {"L2", {"ONE"}}, {"R2", {"THREE"}},
        {"L3", {"LEFT_SHIFT"}}, {"R3", {"F"}}, {"START", {"ENTER"}}, {"SELECT", {"TAB"}},
        {"LSTICK_UP", {"W"}}, {"LSTICK_DOWN", {"S"}}, {"LSTICK_LEFT", {"A"}}, {"LSTICK_RIGHT", {"D"}},
        {"RSTICK_UP", {"I"}}, {"RSTICK_DOWN", {"K"}}, {"RSTICK_LEFT", {"J"}}, {"RSTICK_RIGHT", {"L"}}};
    return K;
}

inline SMap smap_update(SMap base, const SMap& upd) {   // dict(base); base.update(upd)
    for (const auto& [k, v] : upd) {
        bool found = false;
        for (auto& [a, b] : base)
            if (a == k) { b = v; found = true; }
        if (!found) base.emplace_back(k, v);
    }
    return base;
}
inline const SMap& ps_labels() {
    static const SMap L = {
        {"LEFT_FACE_UP", "D-pad up"}, {"LEFT_FACE_DOWN", "D-pad down"},
        {"LEFT_FACE_LEFT", "D-pad left"}, {"LEFT_FACE_RIGHT", "D-pad right"},
        {"RIGHT_FACE_DOWN", "Cross"}, {"RIGHT_FACE_RIGHT", "Circle"},
        {"RIGHT_FACE_LEFT", "Square"}, {"RIGHT_FACE_UP", "Triangle"},
        {"LEFT_TRIGGER_1", "L1"}, {"RIGHT_TRIGGER_1", "R1"},
        {"LEFT_TRIGGER_2", "L2"}, {"RIGHT_TRIGGER_2", "R2"},
        {"LEFT_THUMB", "L3 (left stick click)"}, {"RIGHT_THUMB", "R3 (right stick click)"},
        {"MIDDLE_LEFT", "Create / Share / Select"}, {"MIDDLE_RIGHT", "Options / Start"},
        {"MIDDLE", "PS button"}, {"LEFT_TRIGGER_AXIS", "L2 (analog)"},
        {"RIGHT_TRIGGER_AXIS", "R2 (analog)"}};
    return L;
}
inline const SMap& xbox_labels() {
    static const SMap L = smap_update(ps_labels(), {
        {"RIGHT_FACE_DOWN", "A"}, {"RIGHT_FACE_RIGHT", "B"}, {"RIGHT_FACE_LEFT", "X"},
        {"RIGHT_FACE_UP", "Y"}, {"LEFT_TRIGGER_1", "LB"}, {"RIGHT_TRIGGER_1", "RB"},
        {"LEFT_TRIGGER_2", "LT"}, {"RIGHT_TRIGGER_2", "RT"}, {"LEFT_THUMB", "LS (left stick click)"},
        {"RIGHT_THUMB", "RS (right stick click)"}, {"MIDDLE_LEFT", "View / Back"},
        {"MIDDLE_RIGHT", "Menu / Start"}, {"MIDDLE", "Xbox button"},
        {"LEFT_TRIGGER_AXIS", "LT (analog)"}, {"RIGHT_TRIGGER_AXIS", "RT (analog)"}});
    return L;
}
inline const SMap& switch_labels() {
    static const SMap L = smap_update(ps_labels(), {
        {"RIGHT_FACE_DOWN", "B (bottom)"}, {"RIGHT_FACE_RIGHT", "A (right)"},
        {"RIGHT_FACE_LEFT", "Y (left)"}, {"RIGHT_FACE_UP", "X (top)"},
        {"LEFT_TRIGGER_1", "L"}, {"RIGHT_TRIGGER_1", "R"}, {"LEFT_TRIGGER_2", "ZL"},
        {"RIGHT_TRIGGER_2", "ZR"}, {"LEFT_THUMB", "left stick click"},
        {"RIGHT_THUMB", "right stick click"}, {"MIDDLE_LEFT", "Minus (-)"},
        {"MIDDLE_RIGHT", "Plus (+)"}, {"MIDDLE", "Home"},
        {"LEFT_TRIGGER_AXIS", "ZL"}, {"RIGHT_TRIGGER_AXIS", "ZR"}});
    return L;
}
inline std::string label_of(const SMap& m, const std::string& s) {
    for (const auto& [k, v] : m)
        if (k == s) return v;
    return s;
}
inline const Named& switch_by_label() {
    static const Named N = [] {
        Named n = positional();
        *nget(n, "CROSS") = {"RIGHT_FACE_RIGHT"};
        *nget(n, "CIRCLE") = {"RIGHT_FACE_DOWN"};
        *nget(n, "SQUARE") = {"RIGHT_FACE_UP"};
        *nget(n, "TRIANGLE") = {"RIGHT_FACE_LEFT"};
        return n;
    }();
    return N;
}

struct Preset { std::string name, title; const Named* buttons; const SMap* labels; std::string gamepad, notes; };
inline const std::vector<Preset>& presets() {
    static const std::vector<Preset> P = {
        {"playstation", "PlayStation (DualSense, DualShock 4, DualShock 3)", &positional(), &ps_labels(), "first",
         "Same buttons in the same places as the DualShock 2."},
        {"xbox", "Xbox (Series, One, 360) and most PC pads", &positional(), &xbox_labels(), "first",
         "Mapped by position: A is Cross, B is Circle, X is Square, Y is Triangle."},
        {"switch", "Nintendo Switch Pro, by position", &positional(), &switch_labels(), "first",
         "By position: the bottom button (Nintendo B) is Cross, the right one "
         "(Nintendo A) is Circle. Use switch-labels to match the printed letters."},
        {"switch-labels", "Nintendo Switch Pro, by printed letter", &switch_by_label(), &switch_labels(), "first",
         "By letter: Nintendo A (east) is Cross, B (south) is Circle, X (north) "
         "is Square, Y (west) is Triangle. If presses land on the wrong button, "
         "your mapping database reports the pad by label already: use switch."},
        {"keyboard", "Keyboard only (gamepads ignored)", &positional(), &ps_labels(), "none",
         "Arrows = D-pad, WASD = left stick, IJKL = right stick, X/Space = Cross, "
         "C = Circle, Z = Square, V = Triangle, Q/E = L1/R1, 1/3 = L2/R2, "
         "Left Shift/F = L3/R3, Enter = Start, Tab = Select."}};
    return P;
}
inline const Preset* find_preset(const std::string& n) {
    for (const auto& p : presets())
        if (p.name == n) return &p;
    return nullptr;
}
inline const std::vector<std::string>& layouts() {
    static const std::vector<std::string> L = [] { std::vector<std::string> v; for (const auto& p : presets()) v.push_back(p.name); return v; }();
    return L;
}

inline Val defaults() {
    Val d = Val::dict();
    d.d->set("layout", Val::str("playstation"));
    d.d->set("buttons", Val::dict());
    d.d->set("keyboard", Val::dict());
    d.d->set("deadzone", Val::flt(0.15));
    d.d->set("invert_y", Val::boolean(false));
    d.d->set("stick_swap", Val::boolean(false));
    d.d->set("trigger_threshold", Val());
    d.d->set("gamepad", Val());
    d.d->set("players", Val::integer(1));
    d.d->set("keyboard_always", Val::boolean(true));
    d.d->set("touch", Val::list());
    d.d->set("touch_on_desktop", Val::boolean(false));
    d.d->set("allow_duplicates", Val::list());
    return d;
}
inline bool known_key(const std::string& k) {
    static const std::vector<std::string> K = {"layout", "buttons", "keyboard", "deadzone", "invert_y", "stick_swap",
                                               "trigger_threshold", "gamepad", "players", "keyboard_always", "touch",
                                               "touch_on_desktop", "allow_duplicates", "$comment", "comment", "notes"};
    return has_s(K, k);
}

inline const std::string MARKER = "generated by console-controller (padmap.py gen)";
inline const std::string DEFAULT_CONFIG = "console/controls.json";

// ---------------------------------------------------------------- config
inline List as_list(const Val& v) {
    if (v.t == Val::NONE) return {};
    if (v.t == Val::LIST) return *v.l;
    return {v};
}
inline bool is_num(const Val& v) { return v.t == Val::INT || v.t == Val::FLOAT; }   // isinstance(v, (int, float)) and not bool
inline double numv(const Val& v) {
    if (v.t == Val::FLOAT) return v.f;
    if (v.big()) return std::strtod(v.s.c_str(), nullptr);
    return static_cast<double>(v.i);
}
inline double to_f(const Val& v) {   // float(v) of a number
    const double d = numv(v);
    if (v.t == Val::INT && v.big() && std::isinf(d)) throw PyErr("OverflowError", "int too large to convert to float");
    return d;
}

struct Zone { std::string name; double x = 0, y = 0, r = 0; };
struct Resolved {
    std::string layout;
    Named buttons, keyboard;
    bool gp_int = false; long long gp_index = 0; std::string gp_str = "first";
    long long players = 1;
    double deadzone_left = 0.15, deadzone_right = 0.15;
    bool invert_left = false, invert_right = false, stick_swap = false;
    bool has_tt = false; double trigger_threshold = 0.0;
    bool keyboard_always = true;
    std::vector<Zone> touch_buttons, touch_sticks;
    bool touch_on_desktop = false;
    std::vector<std::string> allow_duplicates;
    bool uses_pad() const { return gp_int || gp_str != "none"; }
};

inline Resolved resolve(const Val& config, std::vector<std::string>& errors, std::vector<std::string>& warnings) {
    Val cfg = defaults();
    for (const auto& [key, value] : config.d->items) {
        if (!known_key(key)) {
            warnings.push_back("unknown setting " + str_repr(key) + " is ignored");
            continue;
        }
        cfg.d->set(key, value);
    }
    auto C = [&](const char* k) -> const Val& { return *cfg.d->get(k); };
    Resolved r;

    const Val& lv = C("layout");
    if (lv.t == Val::LIST || lv.t == Val::DICT) throw PyErr("TypeError", std::string("unhashable type: '") + type_name(lv) + "'");
    std::string layout = lv.t == Val::STR ? lv.s : "";
    if (lv.t != Val::STR || !find_preset(layout)) {
        errors.push_back("layout " + repr(lv) + " is not one of: " + join(layouts(), ", "));
        layout = "playstation";
    }
    const Preset& preset = *find_preset(layout);
    r.layout = layout;

    // gamepad buttons
    for (const auto& name : ds2_order()) r.buttons.emplace_back(name, nref(*preset.buttons, name));
    const Val bo = C("buttons").t == Val::NONE ? Val::dict() : C("buttons");
    if (bo.t != Val::DICT) {
        errors.push_back("buttons must be an object of DS2 button -> raylib button");
    } else {
        for (const auto& [name, value] : bo.d->items) {
            const std::string uname = upper(name);
            if (ds2_mask(uname) < 0) {
                errors.push_back("buttons: " + str_repr(name) + " is not a DS2 button (" + join(ds2_order(), ", ") + ")");
                continue;
            }
            std::vector<std::string> srcs;
            for (const auto& src : as_list(value)) {
                std::string usrc = upper(pystr(src));
                if (usrc.rfind("GAMEPAD_BUTTON_", 0) == 0) usrc = usrc.substr(15);
                if (!has_s(gamepad_sources(), usrc)) {
                    errors.push_back("buttons." + uname + ": " + repr(src) + " is not a raylib gamepad button (see padmap.py table)");
                    continue;
                }
                srcs.push_back(usrc);
            }
            *nget(r.buttons, uname) = srcs;
        }
    }

    // keyboard
    r.keyboard = keyboard_default();
    const Val ko = C("keyboard").t == Val::NONE ? Val::dict() : C("keyboard");
    if (ko.t != Val::DICT) {
        errors.push_back("keyboard must be an object of DS2 button or stick direction -> key names");
    } else {
        for (const auto& [name, value] : ko.d->items) {
            const std::string uname = upper(name);
            if (ds2_mask(uname) < 0 && !is_stick(uname)) {
                errors.push_back("keyboard: " + str_repr(name) + " is not a DS2 button or LSTICK_/RSTICK_ direction");
                continue;
            }
            std::vector<std::string> keys;
            for (const auto& k : as_list(value)) {
                std::string ukey = upper(pystr(k));
                if (ukey.rfind("KEY_", 0) == 0) ukey = ukey.substr(4);
                if (!has_s(raylib_keys(), ukey)) {
                    errors.push_back("keyboard." + uname + ": " + repr(k) + " is not a raylib key name");
                    continue;
                }
                keys.push_back(ukey);
            }
            *nget(r.keyboard, uname) = keys;
        }
    }

    // gamepad selection
    const Val gv = C("gamepad").t == Val::NONE ? Val::str(preset.gamepad) : C("gamepad");
    if (gv.t == Val::STR && (gv.s == "first" || gv.s == "none")) {
        r.gp_str = gv.s;
    } else if (gv.t == Val::INT && !gv.big() && gv.i >= 0 && gv.i <= 3) {
        r.gp_int = true;
        r.gp_index = gv.i;
    } else {
        errors.push_back("gamepad must be \"first\", \"none\" or a raylib pad index 0 to 3");
        r.gp_str = "first";
    }

    const Val& pv = C("players");
    if (pv.t == Val::INT && !pv.big() && (pv.i == 1 || pv.i == 2)) r.players = pv.i;
    else if (pv.t == Val::FLOAT && (pv.f == 1.0 || pv.f == 2.0)) r.players = static_cast<long long>(pv.f);
    else {
        errors.push_back("players must be 1 or 2");
        r.players = 1;
    }

    // sticks
    const Val& dz = C("deadzone");
    std::vector<std::pair<std::string, Val>> pairs;
    if (dz.t == Val::DICT) {
        const Val* lft = dz.d->get("left");
        const Val* rgt = dz.d->get("right");
        pairs = {{"left", lft ? *lft : Val::flt(0.15)}, {"right", rgt ? *rgt : Val::flt(0.15)}};
    } else {
        pairs = {{"left", dz}, {"right", dz}};
    }
    std::vector<double> dzv;
    for (const auto& [label, val] : pairs) {
        if (!is_num(val) || !(numv(val) >= 0.0 && numv(val) <= 0.9)) {
            errors.push_back("deadzone (" + label + ") must be a number from 0 to 0.9");
            dzv.push_back(0.15);
        } else {
            dzv.push_back(to_f(val));
        }
    }
    r.deadzone_left = dzv[0];
    r.deadzone_right = dzv[1];

    const Val& inv = C("invert_y");
    std::string inv_key;
    if (inv.t == Val::BOOL) inv_key = inv.b ? "true" : "false";
    else if (inv.t == Val::STR && has_s({"left", "right", "both", "none"}, lower(inv.s))) inv_key = lower(inv.s);
    else {
        errors.push_back("invert_y must be false, true (the right stick), \"left\", \"right\" or \"both\"");
        inv_key = "false";
    }
    r.invert_left = inv_key == "left" || inv_key == "both";
    r.invert_right = inv_key == "true" || inv_key == "right" || inv_key == "both";

    if (C("stick_swap").t != Val::BOOL) errors.push_back("stick_swap must be true or false");
    if (C("keyboard_always").t != Val::BOOL) errors.push_back("keyboard_always must be true or false");
    if (C("touch_on_desktop").t != Val::BOOL) errors.push_back("touch_on_desktop must be true or false");

    const Val& tt = C("trigger_threshold");
    if (tt.t != Val::NONE) {
        if (!is_num(tt) || !(numv(tt) > 0.0 && numv(tt) <= 1.0)) {
            errors.push_back("trigger_threshold must be null or a number above 0 and at most 1");
        } else {
            r.has_tt = true;
            r.trigger_threshold = to_f(tt);
        }
    }
    if (r.has_tt) {
        // Analog triggers: L2/R2 read the trigger axes instead of raylib's trigger buttons.
        for (auto& s : *nget(r.buttons, "L2"))
            if (s == "LEFT_TRIGGER_2") s = "LEFT_TRIGGER_AXIS";
        for (auto& s : *nget(r.buttons, "R2"))
            if (s == "RIGHT_TRIGGER_2") s = "RIGHT_TRIGGER_AXIS";
    }

    // touch zones
    const Val zones = C("touch").t == Val::NONE ? Val::list() : C("touch");
    if (zones.t != Val::LIST) {
        errors.push_back("touch must be a list of zones");
    } else {
        for (std::size_t i = 0; i < zones.l->size(); ++i) {
            const Val& zone = (*zones.l)[i];
            const std::string where = "touch[" + std::to_string(i) + "]";
            if (zone.t != Val::DICT) {
                errors.push_back(where + " must be an object");
                continue;
            }
            double nums[3] = {0, 0, 0};
            bool bad = false;
            const char* keys[3] = {"x", "y", "r"};
            for (int k = 0; k < 3; ++k) {
                const Val* p = zone.d->get(keys[k]);
                if (!p || !is_num(*p)) {
                    errors.push_back(where + "." + keys[k] + " must be a number");
                    bad = true;
                    break;
                }
                nums[k] = to_f(*p);
            }
            if (bad) continue;
            if (!(nums[0] >= 0.0 && nums[0] <= 1.0 && nums[1] >= 0.0 && nums[1] <= 1.0)) {
                errors.push_back(where + ": x and y are fractions of the screen, 0 to 1");
                continue;
            }
            if (!(nums[2] > 0.0 && nums[2] <= 0.5)) {
                errors.push_back(where + ": r is a fraction of the shorter screen side, above 0 and at most 0.5");
                continue;
            }
            const Val* bv = zone.d->get("button");
            const Val* sv = zone.d->get("stick");
            if (bv && !sv) {
                const std::string b = upper(pystr(*bv));
                if (ds2_mask(b) < 0) {
                    errors.push_back(where + ".button " + repr(*bv) + " is not a DS2 button");
                    continue;
                }
                r.touch_buttons.push_back({b, nums[0], nums[1], nums[2]});
            } else if (sv && !bv) {
                const std::string s = lower(pystr(*sv));
                if (s != "left" && s != "right") {
                    errors.push_back(where + ".stick must be \"left\" or \"right\"");
                    continue;
                }
                r.touch_sticks.push_back({s, nums[0], nums[1], nums[2]});
            } else {
                errors.push_back(where + " needs exactly one of \"button\" or \"stick\"");
            }
        }
    }

    std::set<std::string> allow;
    for (const auto& item : as_list(C("allow_duplicates"))) {
        const std::string u = upper(pystr(item));
        allow.insert(u.rfind("KEY_", 0) == 0 ? u.substr(4) : u);
    }
    r.allow_duplicates.assign(allow.begin(), allow.end());
    r.stick_swap = C("stick_swap").t == Val::BOOL && C("stick_swap").b;
    r.keyboard_always = C("keyboard_always").t == Val::BOOL ? C("keyboard_always").b : true;
    r.touch_on_desktop = C("touch_on_desktop").t == Val::BOOL && C("touch_on_desktop").b;
    return r;
}

inline void validate(const Resolved& r, std::vector<std::string>& errors, std::vector<std::string>& warnings) {
    const std::set<std::string> allow(r.allow_duplicates.begin(), r.allow_duplicates.end());
    const bool uses_pad = r.uses_pad();
    if (uses_pad) {
        for (const auto& name : ds2_order())
            if (nref(r.buttons, name).empty()) errors.push_back(name + " is not bound to any gamepad button");
        std::map<std::string, std::vector<std::string>> owners;
        for (const auto& name : ds2_order())
            for (const auto& src : nref(r.buttons, name)) owners[src].push_back(name);
        for (const auto& [src, names] : owners)
            if (names.size() > 1 && !allow.count(src))
                errors.push_back("gamepad " + src + " is bound to " + join(names, " and ") + " (add it to allow_duplicates if you mean it)");
        for (const auto& name : ds2_order())
            if (has_s(nref(r.buttons, name), "MIDDLE"))
                warnings.push_back(name + " uses the guide button (MIDDLE): Steam, Windows and macOS often "
                                          "take that press for themselves");
    }
    for (const auto& name : ds2_order()) {
        if (nref(r.keyboard, name).empty()) {
            const std::string msg = name + " has no keyboard key";
            (uses_pad ? warnings : errors).push_back(msg);
        }
    }
    std::map<std::string, std::vector<std::string>> key_owners;
    for (const auto& [target, keys] : r.keyboard)
        for (const auto& k : keys) key_owners[k].push_back(target);
    for (const auto& [k, targets] : key_owners)
        if (targets.size() > 1 && !allow.count(k))
            errors.push_back("key " + k + " is bound to " + join(targets, " and ") + " (add it to allow_duplicates if you mean it)");
    if (key_owners.count("ESCAPE"))
        warnings.push_back("ESCAPE is bound, but raylib closes the window on Escape (the runtime "
                           "never calls SetExitKey), so that press also quits the game");
    if (key_owners.count("F1"))
        warnings.push_back("F1 is bound, but desktop builds with the debug UI toggle the debug "
                           "panel on F1 (ps2_debug_panel.cpp)");
    if (!r.keyboard_always && !uses_pad)
        warnings.push_back("keyboard_always is false but gamepads are ignored: the keyboard is used anyway");
    if (r.players == 2 && !uses_pad) errors.push_back("players 2 needs gamepads (gamepad is \"none\")");
    if (r.touch_on_desktop && r.touch_buttons.empty() && r.touch_sticks.empty())
        warnings.push_back("touch_on_desktop is true but there are no touch zones");
}

// ---------------------------------------------------------------- replay scripts
// int(text, base) as Python parses it: surrounding whitespace, a sign, "0x" for base 16, single underscores between
// digits, and any Unicode decimal digit. digits is the normalized magnitude (no leading zeros).
struct PyInt { bool ok = false; bool neg = false; std::string digits; };
inline PyInt py_int(const std::string& s0, int base) {
    PyInt o;
    std::string s;
    for (const auto& ch : cps(strip(s0))) {
        const uint32_t c = cp_of(ch);
        if (c < 0x80) { s += static_cast<char>(c); continue; }
        bool hit = false;
        for (const auto& rg : ucase::DECIMAL)
            if (c >= rg.lo && c <= rg.hi) { s += static_cast<char>('0' + (c - rg.lo) % 10); hit = true; break; }
        if (!hit) return o;
    }
    std::size_t k = 0;
    if (!s.empty() && (s[0] == '+' || s[0] == '-')) { o.neg = s[0] == '-'; k = 1; }
    auto dv = [&](char c) -> int {
        int v = -1;
        if (c >= '0' && c <= '9') v = c - '0';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 10;
        else if (c >= 'A' && c <= 'Z') v = c - 'A' + 10;
        return v >= 0 && v < base ? v : -1;
    };
    if (base == 16 && k + 1 < s.size() && s[k] == '0' && (s[k + 1] == 'x' || s[k + 1] == 'X')) {
        k += 2;
        if (k < s.size() && s[k] == '_') ++k;
    }
    std::string d;
    bool prev = false;
    for (std::size_t j = k; j < s.size(); ++j) {
        const char c = s[j];
        if (dv(c) >= 0) { d += static_cast<char>(std::tolower(static_cast<unsigned char>(c))); prev = true; }
        else if (c == '_' && prev && j + 1 < s.size() && dv(s[j + 1]) >= 0) prev = false;
        else return o;
    }
    if (d.empty()) return o;
    const std::size_t nz = d.find_first_not_of('0');
    o.digits = nz == std::string::npos ? "0" : d.substr(nz);
    if (o.digits == "0") o.neg = false;
    o.ok = true;
    return o;
}
// the magnitude if it fits under limit (else false)
inline bool py_int_small(const PyInt& p, int base, unsigned long long limit, unsigned long long& v) {
    if (p.digits.size() > 18) return false;
    v = std::stoull(p.digits, nullptr, base);
    return v <= limit;
}

inline int parse_button_token(const std::string& token) {   // the PRESSED mask (active-high, DS2 bit values)
    if (token == "-" || token == "none" || token == "NONE") return 0;
    const auto cp = cps(token);
    std::string head;
    for (std::size_t i = 0; i < cp.size() && i < 2; ++i) head += cp[i];
    if (lower(head) == "0x") {
        const PyInt p = py_int(token.substr(2), 16);
        if (!p.ok) throw ScriptError("bad hex mask " + str_repr(token));
        unsigned long long v = 0;
        if (p.neg || !py_int_small(p, 16, 0xFFFF, v)) throw ScriptError("mask " + str_repr(token) + " is above 0xFFFF");
        return static_cast<int>(v);
    }
    int mask = 0;
    for (const auto& part : split(token, "+")) {
        const int m = ds2_mask(upper(part));
        if (m < 0) throw ScriptError(str_repr(part) + " is not a DS2 button");
        mask |= m;
    }
    return mask;
}
inline int parse_byte(const std::string& token) {
    const auto cp = cps(token);
    std::string head;
    for (std::size_t i = 0; i < cp.size() && i < 2; ++i) head += cp[i];
    const int base = lower(head) == "0x" ? 16 : 10;
    const PyInt p = py_int(token, base);
    if (!p.ok) throw ScriptError("stick value " + str_repr(token) + " is not a number");
    unsigned long long v = 0;
    if (p.neg || !py_int_small(p, base, 255, v)) throw ScriptError("stick value " + str_repr(token) + " is outside 0 to 255");
    return static_cast<int>(v);
}

struct ScriptLine { long long frame; int pressed, lx, ly, rx, ry; };
inline std::vector<ScriptLine> parse_script(const std::string& text) {
    std::vector<ScriptLine> lines;
    long long last_frame = -1;
    long long number = 0;
    for (const auto& raw : splitlines(text)) {
        ++number;
        const std::string line = strip(split(raw, "#", 1)[0]);
        if (line.empty()) continue;
        const auto parts = split_ws(line);
        const std::string at = "line " + std::to_string(number) + ": ";
        if (parts.size() != 2 && parts.size() != 6)
            throw ScriptError(at + "expected 'frame buttons' or 'frame buttons lx ly rx ry'");
        const PyInt f = py_int(parts[0], 10);
        if (!f.ok) throw ScriptError(at + "frame " + str_repr(parts[0]) + " is not a whole number");
        unsigned long long fv = 0;
        if (f.neg || !py_int_small(f, 10, 0xFFFFFFFFULL, fv))
            throw ScriptError(at + "frame " + (f.neg ? "-" : "") + f.digits + " is out of range");
        const long long frame = static_cast<long long>(fv);
        if (frame < last_frame)
            throw ScriptError(at + "frame " + std::to_string(frame) + " comes before the previous line's " + std::to_string(last_frame));
        ScriptLine s{frame, 0, 128, 128, 128, 128};
        try {
            s.pressed = parse_button_token(parts[1]);
            if (parts.size() == 6) {
                s.lx = parse_byte(parts[2]);
                s.ly = parse_byte(parts[3]);
                s.rx = parse_byte(parts[4]);
                s.ry = parse_byte(parts[5]);
            }
        } catch (const ScriptError& e) {
            throw ScriptError(at + e.what());
        }
        lines.push_back(s);
        last_frame = frame;
    }
    return lines;
}

// ---------------------------------------------------------------- C++ generation
inline std::string cfloat(double value) {
    std::string t = float_repr(value);
    if (t.find_first_of("eE") != std::string::npos) t = cfmt("%.9f", value);
    return t + "f";
}
inline void sentinel(std::vector<std::string>& L, const std::vector<std::string>& rows, const std::string& empty) {
    if (rows.empty()) {
        L.push_back("        " + empty + ", // none");
        return;
    }
    for (const auto& r : rows) L.push_back("        " + r + ",");
}
inline std::string tf(bool b) { return b ? "true" : "false"; }

inline const std::string REPLAY_CPP = R"PADMAP_CPP(
    // ---- input replay (PS2X_INPUT_SCRIPT) and recording (PS2X_INPUT_RECORD) ----
    struct ScriptLine
    {
        uint32_t frame;
        uint16_t pressed; // active-high here, flipped when written to the report
        uint8_t lx, ly, rx, ry;
    };

    struct NamedMask
    {
        const char *name;
        uint16_t mask;
    };

    constexpr NamedMask kScriptButtons[] = {
        {"UP", PAD_UP}, {"DOWN", PAD_DOWN}, {"LEFT", PAD_LEFT}, {"RIGHT", PAD_RIGHT},
        {"CROSS", PAD_CROSS}, {"CIRCLE", PAD_CIRCLE}, {"SQUARE", PAD_SQUARE},
        {"TRIANGLE", PAD_TRIANGLE}, {"L1", PAD_L1}, {"R1", PAD_R1}, {"L2", PAD_L2},
        {"R2", PAD_R2}, {"L3", PAD_L3}, {"R3", PAD_R3}, {"START", PAD_START},
        {"SELECT", PAD_SELECT},
    };

    struct ReplayState
    {
        std::mutex lock;
        bool initialized = false;
        bool active = false;
        bool announcedEnd = false;
        std::vector<ScriptLine> lines;
        size_t next = 0;
        uint32_t frame = 0;
        Report current{};
        std::FILE *record = nullptr;
        bool haveLast = false;
        Report last{};
    };

    ReplayState &replayState()
    {
        static ReplayState state;
        return state;
    }

    std::vector<std::string> splitWords(const std::string &line)
    {
        std::vector<std::string> words;
        std::string word;
        for (char c : line)
        {
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            {
                if (!word.empty())
                    words.push_back(word);
                word.clear();
            }
            else
            {
                word.push_back(c);
            }
        }
        if (!word.empty())
            words.push_back(word);
        return words;
    }

    bool parseNumber(const std::string &text, unsigned long max, unsigned long &out)
    {
        if (text.empty())
            return false;
        int base = 10;
        size_t start = 0;
        if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
        {
            base = 16;
            start = 2;
        }
        char *end = nullptr;
        const unsigned long value = std::strtoul(text.c_str() + start, &end, base);
        if (!end || *end != '\0' || end == text.c_str() + start || value > max)
            return false;
        out = value;
        return true;
    }

    bool parseButtons(const std::string &token, uint16_t &pressed)
    {
        pressed = 0u;
        if (token == "-" || token == "none" || token == "NONE")
            return true;
        if (token.size() > 2 && token[0] == '0' && (token[1] == 'x' || token[1] == 'X'))
        {
            unsigned long mask = 0;
            if (!parseNumber(token, 0xFFFFul, mask))
                return false;
            pressed = static_cast<uint16_t>(mask);
            return true;
        }
        size_t start = 0;
        while (true)
        {
            const size_t plus = token.find('+', start);
            std::string name = token.substr(start, plus == std::string::npos ? std::string::npos : plus - start);
            for (char &c : name)
            {
                if (c >= 'a' && c <= 'z')
                    c = static_cast<char>(c - 'a' + 'A');
            }
            bool found = false;
            for (const auto &entry : kScriptButtons)
            {
                if (name == entry.name)
                {
                    pressed = static_cast<uint16_t>(pressed | entry.mask);
                    found = true;
                    break;
                }
            }
            if (!found)
                return false;
            if (plus == std::string::npos)
                return true;
            start = plus + 1;
        }
    }

    bool loadScript(const char *path, std::vector<ScriptLine> &out)
    {
        std::FILE *file = std::fopen(path, "r");
        if (!file)
        {
            std::fprintf(stderr, "[pad-replay] cannot open %s\n", path);
            return false;
        }
        char buffer[512];
        unsigned lineNumber = 0;
        uint32_t lastFrame = 0;
        bool ok = true;
        while (std::fgets(buffer, sizeof(buffer), file))
        {
            ++lineNumber;
            std::string line(buffer);
            const size_t hash = line.find('#');
            if (hash != std::string::npos)
                line.erase(hash);
            const std::vector<std::string> words = splitWords(line);
            if (words.empty())
                continue;
            ScriptLine entry{0u, 0u, kPadStickCenter, kPadStickCenter, kPadStickCenter, kPadStickCenter};
            unsigned long value = 0;
            bool lineOk = (words.size() == 2 || words.size() == 6) &&
                          parseNumber(words[0], 0xFFFFFFFFul, value);
            if (lineOk)
                entry.frame = static_cast<uint32_t>(value);
            lineOk = lineOk && parseButtons(words[1], entry.pressed);
            if (lineOk && words.size() == 6)
            {
                uint8_t *sticks[4] = {&entry.lx, &entry.ly, &entry.rx, &entry.ry};
                for (int i = 0; i < 4 && lineOk; ++i)
                {
                    lineOk = parseNumber(words[2 + i], 255ul, value);
                    *sticks[i] = static_cast<uint8_t>(value);
                }
            }
            if (lineOk && !out.empty() && entry.frame < lastFrame)
                lineOk = false;
            if (!lineOk)
            {
                std::fprintf(stderr, "[pad-replay] %s line %u is not 'frame buttons [lx ly rx ry]' in frame order; replay disabled\n",
                             path, lineNumber);
                ok = false;
                break;
            }
            lastFrame = entry.frame;
            out.push_back(entry);
        }
        std::fclose(file);
        if (!ok)
            out.clear();
        return ok;
    }

    void initReplayLocked(ReplayState &state)
    {
        if (state.initialized)
            return;
        state.initialized = true;
        const char *script = std::getenv("PS2X_INPUT_SCRIPT");
        if (script && script[0] != '\0')
        {
            if (loadScript(script, state.lines))
            {
                state.active = true;
                std::fprintf(stderr, "[pad-replay] loaded %u lines from %s\n",
                             static_cast<unsigned>(state.lines.size()), script);
            }
            return;
        }
        const char *recordPath = std::getenv("PS2X_INPUT_RECORD");
        if (recordPath && recordPath[0] != '\0')
        {
            state.record = std::fopen(recordPath, "w");
            if (state.record)
            {
                std::fprintf(state.record, "# recorded by ps2_pad.cpp: frame buttons lx ly rx ry (one line per change)\n");
                std::fflush(state.record);
                std::fprintf(stderr, "[pad-record] writing %s\n", recordPath);
            }
            else
            {
                std::fprintf(stderr, "[pad-record] cannot open %s\n", recordPath);
            }
        }
    }

    // Port 0 only. Returns true and fills the report while a script is playing.
    bool replayReport(Report &out)
    {
        ReplayState &state = replayState();
        std::lock_guard<std::mutex> guard(state.lock);
        initReplayLocked(state);
        if (!state.active)
            return false;
        while (state.next < state.lines.size() && state.lines[state.next].frame <= state.frame)
        {
            const ScriptLine &line = state.lines[state.next];
            state.current.buttons = static_cast<uint16_t>(~line.pressed);
            state.current.lx = line.lx;
            state.current.ly = line.ly;
            state.current.rx = line.rx;
            state.current.ry = line.ry;
            ++state.next;
        }
        if (state.next >= state.lines.size() && !state.announcedEnd)
        {
            state.announcedEnd = true;
            std::fprintf(stderr, "[pad-replay] script finished at frame %u\n", state.frame);
        }
        out = state.current;
        ++state.frame;
        return true;
    }

    void recordReport(const Report &report)
    {
        ReplayState &state = replayState();
        std::lock_guard<std::mutex> guard(state.lock);
        const uint32_t frame = state.frame++;
        if (!state.record)
            return;
        if (state.haveLast && state.last.buttons == report.buttons && state.last.lx == report.lx &&
            state.last.ly == report.ly && state.last.rx == report.rx && state.last.ry == report.ry)
            return;
        state.haveLast = true;
        state.last = report;
        std::string names;
        const uint16_t pressed = static_cast<uint16_t>(~report.buttons);
        for (const auto &entry : kScriptButtons)
        {
            if (pressed & entry.mask)
            {
                if (!names.empty())
                    names.push_back('+');
                names += entry.name;
            }
        }
        if (names.empty())
            names = "-";
        std::fprintf(state.record, "%u %s %u %u %u %u\n", frame, names.c_str(),
                     static_cast<unsigned>(report.lx), static_cast<unsigned>(report.ly),
                     static_cast<unsigned>(report.rx), static_cast<unsigned>(report.ry));
        std::fflush(state.record);
    }
)PADMAP_CPP";

inline std::string generate_cpp(const Resolved& r, bool replay, const std::string& source) {
    const long long gamepad_index = r.gp_int ? r.gp_index : (r.gp_str == "none" ? -2 : -1);
    const bool touch = !r.touch_buttons.empty() || !r.touch_sticks.empty();
    std::vector<std::string> button_rows, axis_rows, key_rows, stick_key_rows;
    for (const auto& name : ds2_order()) {
        for (const auto& src : nref(r.buttons, name)) {
            const std::string ax = trigger_axis(src);
            if (!ax.empty()) axis_rows.push_back("{PAD_" + name + ", " + ax + "}");
            else button_rows.push_back("{PAD_" + name + ", GAMEPAD_BUTTON_" + src + "}");
        }
    }
    for (const auto& name : ds2_order())
        for (const auto& key : nref(r.keyboard, name)) key_rows.push_back("{PAD_" + name + ", KEY_" + key + "}");
    std::vector<StickDir> dirs = stick_dirs();
    std::sort(dirs.begin(), dirs.end(), [](const StickDir& a, const StickDir& b) { return a.name < b.name; });
    for (const auto& d : dirs)
        for (const auto& key : nref(r.keyboard, d.name))
            stick_key_rows.push_back("{" + std::to_string(d.stick) + ", " + std::to_string(d.axis) + ", " + cfloat(d.value) +
                                     ", KEY_" + key + "} /* " + d.name + " */");

    std::vector<std::string> L;
    auto w = [&L](const std::string& s) { L.push_back(s); };
    w("// ps2_pad.cpp: " + MARKER + " from " + source + ".");
    w("// Layout preset: " + r.layout + ". Edit controls.json and run padmap.py gen again; do not edit this file.");
    w("//");
    w("// Drop-in replacement for ps2xRuntime/src/lib/ps2_pad.cpp in PS2Recomp. Same class, same");
    w("// readState signature, same 32-byte DualShock 2 report: data[1] = 0x73 (analog), buttons");
    w("// ACTIVE-LOW in data[2] (low byte) and data[3] (high byte), sticks in data[4..7] as");
    w("// RX, RY, LX, LY with 0x80 at centre. raylib calls only. Back up the stock file first");
    w("// (padmap.py install does that). Changing this .cpp is an incremental rebuild, no headers.");
    if (replay) {
        w("//");
        w("// Input replay: set PS2X_INPUT_SCRIPT to a file of 'frame buttons [lx ly rx ry]' lines and");
        w("// port 0 plays it back instead of live input (frame = number of port-0 readState calls).");
        w("// Input recording: set PS2X_INPUT_RECORD to a path and live port-0 input is written there");
        w("// in the same format, one line per change.");
    }
    if (replay) {
        w("#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)");
        w("#define _CRT_SECURE_NO_WARNINGS");
        w("#endif");
    }
    if (touch && r.touch_on_desktop) {
        w("#ifndef PS2X_PAD_TOUCH");
        w("#define PS2X_PAD_TOUCH 1");
        w("#endif");
    }
    w("#include \"runtime/ps2_pad.h\"");
    w("#include \"ps2_host_backend.h\"");
    w("#include <cstring>");
    w("#include <atomic>");
    w("#include <cmath>");
    if (replay) {
        w("#include <cstdio>");
        w("#include <cstdlib>");
        w("#include <mutex>");
        w("#include <string>");
        w("#include <vector>");
    }
    w("");
    w("namespace");
    w("{");
    w("    constexpr uint8_t kPadAnalogMarker = 0x73;");
    w("    constexpr uint8_t kPadStickCenter = 0x80;");
    w("");
    for (const char* name : {"LEFT", "DOWN", "RIGHT", "UP", "START", "R3", "L3", "SELECT", "SQUARE", "CROSS", "CIRCLE", "TRIANGLE", "R1", "L1", "R2", "L2"}) {
        w(std::string("    constexpr uint16_t PAD_") + name + " = " + cfmt("0x%04Xu", static_cast<unsigned>(ds2_mask(name))) + ";");
    }
    w("");
    w("    // ---- settings from " + source + " ----");
    w("    constexpr int kGamepadIndex = " + std::to_string(gamepad_index) + "; // -1 = first connected pads in order, -2 = gamepads ignored");
    w("    constexpr int kPlayers = " + std::to_string(r.players) + "; // port 1 gets the second pad when 2");
    w("    constexpr bool kKeyboardAlways = " + tf(r.keyboard_always) + "; // keyboard also works while a pad is connected");
    w("    constexpr float kDeadzoneLeft = " + cfloat(r.deadzone_left) + "; // radial");
    w("    constexpr float kDeadzoneRight = " + cfloat(r.deadzone_right) + ";");
    w("    constexpr bool kInvertLeftY = " + tf(r.invert_left) + ";");
    w("    constexpr bool kInvertRightY = " + tf(r.invert_right) + ";");
    w("    constexpr bool kStickSwap = " + tf(r.stick_swap) + ";");
    w("    constexpr float kTriggerThreshold = " + cfloat(r.has_tt ? r.trigger_threshold : 0.5) + "; // analog L2/R2 bindings only");
    w("");
    w("    struct ButtonBinding");
    w("    {");
    w("        uint16_t mask;");
    w("        int button;");
    w("    };");
    w("    struct AxisBinding");
    w("    {");
    w("        uint16_t mask;");
    w("        int axis;");
    w("    };");
    w("    struct KeyBinding");
    w("    {");
    w("        uint16_t mask;");
    w("        int key;");
    w("    };");
    w("    struct StickKey");
    w("    {");
    w("        int stick; // 0 = left, 1 = right");
    w("        int axis;  // 0 = x, 1 = y");
    w("        float value;");
    w("        int key;");
    w("    };");
    w("");
    w("    // A row with mask 0 (or key -1) is a placeholder and is skipped.");
    w("    constexpr ButtonBinding kGamepadButtons[] = {");
    sentinel(L, button_rows, "{0u, 0}");
    w("    };");
    w("    constexpr AxisBinding kTriggerAxes[] = {");
    sentinel(L, axis_rows, "{0u, 0}");
    w("    };");
    w("    constexpr KeyBinding kKeyButtons[] = {");
    sentinel(L, key_rows, "{0u, -1}");
    w("    };");
    w("    constexpr StickKey kStickKeys[] = {");
    sentinel(L, stick_key_rows, "{0, 0, 0.0f, -1}");
    w("    };");
    w("");
    if (touch) {
        w("#if defined(__ANDROID__) || defined(PS2X_PAD_TOUCH)");
        w("    // Touch zones: x, y are fractions of the screen; r is a fraction of its shorter side.");
        w("    // They are invisible: the runtime draws no overlay (ps2_android_runtime.cpp is a TODO).");
        w("    struct TouchButton");
        w("    {");
        w("        uint16_t mask;");
        w("        float x, y, r;");
        w("    };");
        w("    struct TouchStick");
        w("    {");
        w("        int stick;");
        w("        float x, y, r;");
        w("    };");
        std::vector<std::string> tb, ts;
        for (const auto& z : r.touch_buttons) tb.push_back("{PAD_" + z.name + ", " + cfloat(z.x) + ", " + cfloat(z.y) + ", " + cfloat(z.r) + "}");
        for (const auto& z : r.touch_sticks) ts.push_back("{" + std::string(z.name == "left" ? "0" : "1") + ", " + cfloat(z.x) + ", " + cfloat(z.y) + ", " + cfloat(z.r) + "}");
        w("    constexpr TouchButton kTouchButtons[] = {");
        sentinel(L, tb, "{0u, 0.0f, 0.0f, 0.0f}");
        w("    };");
        w("    constexpr TouchStick kTouchSticks[] = {");
        sentinel(L, ts, "{-1, 0.0f, 0.0f, 0.0f}");
        w("    };");
        w("#endif");
        w("");
    }
    w("    struct PadFrame");
    w("    {");
    w("        uint16_t buttons = 0xFFFFu; // active-low, like the report");
    w("        float lx = 0.0f;");
    w("        float ly = 0.0f;");
    w("        float rx = 0.0f;");
    w("        float ry = 0.0f;");
    w("    };");
    w("");
    w("    struct Report");
    w("    {");
    w("        uint16_t buttons = 0xFFFFu; // active-low");
    w("        uint8_t lx = kPadStickCenter;");
    w("        uint8_t ly = kPadStickCenter;");
    w("        uint8_t rx = kPadStickCenter;");
    w("        uint8_t ry = kPadStickCenter;");
    w("    };");
    w("");
    w("    std::atomic<bool> g_triggerSignedRange[4][2]{}; // [pad][left, right trigger]");
    w("");
    w("    float clampUnit(float v)");
    w("    {");
    w("        return v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);");
    w("    }");
    w("");
    w("    uint8_t axisToByte(float v)");
    w("    {");
    w("        return static_cast<uint8_t>(std::lround((clampUnit(v) + 1.0f) * 127.5f));");
    w("    }");
    w("");
    w("    void applyRadialDeadzone(float &x, float &y, float deadzone)");
    w("    {");
    w("        const float magnitude = std::sqrt(x * x + y * y);");
    w("        if (magnitude <= deadzone || magnitude <= 0.0f)");
    w("        {");
    w("            x = 0.0f;");
    w("            y = 0.0f;");
    w("            return;");
    w("        }");
    w("        const float clamped = magnitude > 1.0f ? 1.0f : magnitude;");
    w("        const float scale = (clamped - deadzone) / (1.0f - deadzone) / magnitude;");
    w("        x = clampUnit(x * scale);");
    w("        y = clampUnit(y * scale);");
    w("    }");
    w("");
    w("    int resolveGamepad(int player)");
    w("    {");
    w("        if (kGamepadIndex == -2)");
    w("            return -1;");
    w("        if (kGamepadIndex >= 0)");
    w("        {");
    w("            const int pad = kGamepadIndex + player;");
    w("            return IsGamepadAvailable(pad) ? pad : -1;");
    w("        }");
    w("        int seen = 0;");
    w("        for (int pad = 0; pad < 4; ++pad)");
    w("        {");
    w("            if (!IsGamepadAvailable(pad))");
    w("                continue;");
    w("            if (seen == player)");
    w("                return pad;");
    w("            ++seen;");
    w("        }");
    w("        return -1;");
    w("    }");
    w("");
    w("    // Trigger axes rest at -1 on some backends and at 0 on others (some report 0 until");
    w("    // first touched): once a value below zero is seen on a trigger, treat its range as");
    w("    // -1..1 and rescale it to 0..1. Tracked per trigger so one never affects the other.");
    w("    float triggerAmount(int pad, int axis)");
    w("    {");
    w("        float v = GetGamepadAxisMovement(pad, axis);");
    w("        std::atomic<bool> &isSigned = g_triggerSignedRange[pad & 3][axis == GAMEPAD_AXIS_RIGHT_TRIGGER ? 1 : 0];");
    w("        if (v < -0.01f)");
    w("            isSigned.store(true, std::memory_order_relaxed);");
    w("        if (isSigned.load(std::memory_order_relaxed))");
    w("            v = (v + 1.0f) * 0.5f;");
    w("        return v;");
    w("    }");
    w("");
    if (touch) {
        w("#if defined(__ANDROID__) || defined(PS2X_PAD_TOUCH)");
        w("    void readTouch(PadFrame &frame)");
        w("    {");
        w("        const int count = GetTouchPointCount();");
        w("        const float width = static_cast<float>(GetScreenWidth());");
        w("        const float height = static_cast<float>(GetScreenHeight());");
        w("        if (count <= 0 || width <= 0.0f || height <= 0.0f)");
        w("            return;");
        w("        const float side = width < height ? width : height;");
        w("        for (int i = 0; i < count; ++i)");
        w("        {");
        w("            const Vector2 point = GetTouchPosition(i);");
        w("            for (const auto &zone : kTouchButtons)");
        w("            {");
        w("                if (zone.mask == 0u)");
        w("                    continue;");
        w("                const float dx = (point.x - zone.x * width) / side;");
        w("                const float dy = (point.y - zone.y * height) / side;");
        w("                if (dx * dx + dy * dy <= zone.r * zone.r)");
        w("                    frame.buttons &= static_cast<uint16_t>(~zone.mask);");
        w("            }");
        w("            for (const auto &zone : kTouchSticks)");
        w("            {");
        w("                if (zone.stick < 0)");
        w("                    continue;");
        w("                const float dx = (point.x - zone.x * width) / side / zone.r;");
        w("                const float dy = (point.y - zone.y * height) / side / zone.r;");
        w("                if (dx * dx + dy * dy > 2.25f) // grab radius: 1.5 x r");
        w("                    continue;");
        w("                if (zone.stick == 0)");
        w("                {");
        w("                    frame.lx = clampUnit(dx);");
        w("                    frame.ly = clampUnit(dy);");
        w("                }");
        w("                else");
        w("                {");
        w("                    frame.rx = clampUnit(dx);");
        w("                    frame.ry = clampUnit(dy);");
        w("                }");
        w("            }");
        w("        }");
        w("    }");
        w("#endif");
        w("");
    }
    w("    void readLive(int player, PadFrame &frame)");
    w("    {");
    w("        bool usedGamepad = false;");
    w("        const int pad = resolveGamepad(player);");
    w("        if (pad >= 0)");
    w("        {");
    w("            usedGamepad = true;");
    w("            for (const auto &b : kGamepadButtons)");
    w("            {");
    w("                if (b.mask != 0u && IsGamepadButtonDown(pad, b.button))");
    w("                    frame.buttons &= static_cast<uint16_t>(~b.mask);");
    w("            }");
    w("            for (const auto &t : kTriggerAxes)");
    w("            {");
    w("                if (t.mask != 0u && triggerAmount(pad, t.axis) >= kTriggerThreshold)");
    w("                    frame.buttons &= static_cast<uint16_t>(~t.mask);");
    w("            }");
    w("            float lx = GetGamepadAxisMovement(pad, GAMEPAD_AXIS_LEFT_X);");
    w("            float ly = GetGamepadAxisMovement(pad, GAMEPAD_AXIS_LEFT_Y);");
    w("            float rx = GetGamepadAxisMovement(pad, GAMEPAD_AXIS_RIGHT_X);");
    w("            float ry = GetGamepadAxisMovement(pad, GAMEPAD_AXIS_RIGHT_Y);");
    w("            if (kStickSwap)");
    w("            {");
    w("                const float tx = lx;");
    w("                const float ty = ly;");
    w("                lx = rx;");
    w("                ly = ry;");
    w("                rx = tx;");
    w("                ry = ty;");
    w("            }");
    w("            frame.lx = lx;");
    w("            frame.ly = ly;");
    w("            frame.rx = rx;");
    w("            frame.ry = ry;");
    w("        }");
    w("");
    w("        if (player == 0 && (kKeyboardAlways || !usedGamepad))");
    w("        {");
    w("            for (const auto &k : kKeyButtons)");
    w("            {");
    w("                if (k.key >= 0 && IsKeyDown(k.key))");
    w("                    frame.buttons &= static_cast<uint16_t>(~k.mask);");
    w("            }");
    w("            float keyAxes[2][2] = {{0.0f, 0.0f}, {0.0f, 0.0f}};");
    w("            for (const auto &s : kStickKeys)");
    w("            {");
    w("                if (s.key >= 0 && IsKeyDown(s.key))");
    w("                    keyAxes[s.stick][s.axis] += s.value;");
    w("            }");
    w("            if (keyAxes[0][0] != 0.0f || keyAxes[0][1] != 0.0f)");
    w("            {");
    w("                frame.lx = clampUnit(keyAxes[0][0]);");
    w("                frame.ly = clampUnit(keyAxes[0][1]);");
    w("            }");
    w("            if (keyAxes[1][0] != 0.0f || keyAxes[1][1] != 0.0f)");
    w("            {");
    w("                frame.rx = clampUnit(keyAxes[1][0]);");
    w("                frame.ry = clampUnit(keyAxes[1][1]);");
    w("            }");
    w("        }");
    if (touch) {
        w("#if defined(__ANDROID__) || defined(PS2X_PAD_TOUCH)");
        w("        if (player == 0)");
        w("            readTouch(frame);");
        w("#endif");
    }
    w("");
    w("        if (kInvertLeftY)");
    w("            frame.ly = -frame.ly;");
    w("        if (kInvertRightY)");
    w("            frame.ry = -frame.ry;");
    w("        applyRadialDeadzone(frame.lx, frame.ly, kDeadzoneLeft);");
    w("        applyRadialDeadzone(frame.rx, frame.ry, kDeadzoneRight);");
    w("    }");
    w("");
    w("    Report toReport(const PadFrame &frame)");
    w("    {");
    w("        Report report;");
    w("        report.buttons = frame.buttons;");
    w("        report.lx = axisToByte(frame.lx);");
    w("        report.ly = axisToByte(frame.ly);");
    w("        report.rx = axisToByte(frame.rx);");
    w("        report.ry = axisToByte(frame.ry);");
    w("        return report;");
    w("    }");
    if (replay) {
        for (const auto& x : split(REPLAY_CPP, "\n")) L.push_back(x);
    }
    w("}");
    w("");
    w("bool PSPadBackend::readState(int port, int slot, uint8_t *data, size_t size)");
    w("{");
    w("    (void)slot;");
    w("    if (!data || size < 32)");
    w("        return false;");
    w("");
    w("    std::memset(data, 0, 32);");
    w("    data[0] = 0x01;");
    w("    data[1] = kPadAnalogMarker;");
    w("    data[2] = 0xFF;");
    w("    data[3] = 0xFF;");
    w("    data[4] = data[5] = data[6] = data[7] = kPadStickCenter;");
    w("");
    w("    Report report; // released, sticks centred: what an unused port reports");
    w("    if (port == 0)");
    w("    {");
    if (replay) {
        w("        if (!replayReport(report))");
        w("        {");
        w("            PadFrame frame;");
        w("            readLive(0, frame);");
        w("            report = toReport(frame);");
        w("            recordReport(report);");
        w("        }");
    } else {
        w("        PadFrame frame;");
        w("        readLive(0, frame);");
        w("        report = toReport(frame);");
    }
    w("    }");
    w("    else if (port > 0 && port < kPlayers)");
    w("    {");
    w("        PadFrame frame;");
    w("        readLive(port, frame);");
    w("        report = toReport(frame);");
    w("    }");
    w("");
    w("    data[2] = static_cast<uint8_t>(report.buttons & 0xFFu);");
    w("    data[3] = static_cast<uint8_t>(report.buttons >> 8);");
    w("    data[4] = report.rx;");
    w("    data[5] = report.ry;");
    w("    data[6] = report.lx;");
    w("    data[7] = report.ly;");
    w("    return true;");
    w("}");
    return join(L, "\n") + "\n";
}

// ---------------------------------------------------------------- printing
inline Val table_rows() {
    List rows;
    for (const auto& name : ds2_order()) {
        const int mask = ds2_mask(name);
        int bit = 0;
        while ((1 << (bit + 1)) <= mask) ++bit;
        const int byte = bit < 8 ? 2 : 3;
        rows.push_back(dict_of({{"button", Val::str(name)}, {"mask", Val::str(cfmt("0x%04X", static_cast<unsigned>(mask)))},
                                {"byte", Val::str("data[" + std::to_string(byte) + "]")}, {"bit", Val::integer(bit % 8)},
                                {"raylib", Val::str("GAMEPAD_BUTTON_" + nref(positional(), name)[0])},
                                {"keyboard", Val::str(join(nref(keyboard_default(), name), "/"))}}));
    }
    return Val::list(std::move(rows));
}

inline void print_table(bool as_json) {
    const Val rows = table_rows();
    if (as_json) {
        out(dumps2(dict_of({{"buttons", rows},
                            {"sticks", dict_of({{"data[4]", Val::str("RX")}, {"data[5]", Val::str("RY")}, {"data[6]", Val::str("LX")},
                                                {"data[7]", Val::str("LY")}, {"centre", Val::str("0x80")},
                                                {"range", Val::str("0x00 (left/up) to 0xFF (right/down)")}})},
                            {"active_low", Val::boolean(true)}})));
        return;
    }
    out("DualShock 2 report (32 bytes) as PS2Recomp's PSPadBackend::readState fills it");
    out("  data[0] = 0x01, data[1] = 0x73 (analog), buttons ACTIVE-LOW: 0 = pressed, 0xFFFF = none");
    out("");
    out(cfmt("  %-9s %-7s %-8s %-4s %-32s %s", "button", "mask", "byte", "bit", "raylib (default)", "keyboard"));
    for (const auto& r : *rows.l)
        out(cfmt("  %-9s %-7s %-8s %-4lld %-32s %s", get(r, "button").s.c_str(), get(r, "mask").s.c_str(), get(r, "byte").s.c_str(),
                 get(r, "bit").i, get(r, "raylib").s.c_str(), get(r, "keyboard").s.c_str()));
    out("");
    out("  sticks: data[4] RX, data[5] RY, data[6] LX, data[7] LY; 0x80 centre,");
    out("          0x00 = full left/up, 0xFF = full right/down");
}

inline Val named_dict(const Named& n) {
    Val d = Val::dict();
    for (const auto& [k, v] : n) d.d->set(k, str_list(v));
    return d;
}

inline void print_presets(const std::vector<std::string>& names, bool as_json) {
    if (as_json) {
        Val o = Val::dict();
        o.d->set("keyboard", named_dict(keyboard_default()));
        for (const auto& name : names) {
            const Preset& p = *find_preset(name);
            Val buttons = Val::dict();
            for (const auto& b : ds2_order()) {
                List l;
                for (const auto& s : nref(*p.buttons, b))
                    l.push_back(dict_of({{"raylib", Val::str(s)}, {"label", Val::str(label_of(*p.labels, s))}}));
                buttons.d->set(b, Val::list(std::move(l)));
            }
            o.d->set(name, dict_of({{"title", Val::str(p.title)}, {"notes", Val::str(p.notes)}, {"gamepad", Val::str(p.gamepad)},
                                    {"buttons", buttons}}));
        }
        out(dumps2(o));
        return;
    }
    for (const auto& name : names) {
        const Preset& p = *find_preset(name);
        out(name + ": " + p.title);
        out("  " + p.notes);
        if (p.gamepad == "none") {
            out("  gamepads ignored; keyboard below");
        } else {
            for (const auto& b : ds2_order()) {
                std::vector<std::string> parts;
                for (const auto& s : nref(*p.buttons, b)) parts.push_back(label_of(*p.labels, s) + " (" + s + ")");
                out(cfmt("  %-9s <- %s", b.c_str(), join(parts, ", ").c_str()));
            }
        }
        out("");
    }
    out("keyboard (every preset, works next to a pad unless keyboard_always is false):");
    for (const auto& [target, keys] : keyboard_default()) out(cfmt("  %-13s <- %s", target.c_str(), join(keys, ", ").c_str()));
}

inline Val starter_config(const std::string& layout) {
    return dict_of({{"$comment", Val::str("console-controller mapping. Edit, then: padmap.py check && padmap.py gen")},
                    {"layout", Val::str(layout)}, {"buttons", Val::dict()}, {"keyboard", Val::dict()},
                    {"deadzone", Val::flt(0.15)}, {"invert_y", Val::boolean(false)}, {"stick_swap", Val::boolean(false)},
                    {"trigger_threshold", Val()}, {"players", Val::integer(1)}, {"keyboard_always", Val::boolean(true)},
                    {"touch", Val::list()}, {"allow_duplicates", Val::list()}});
}

// ---------------------------------------------------------------- files
inline void py_makedirs(const std::string& name) {   // os.makedirs(name, exist_ok=True)
    std::string n = name;
    while (n.size() > 1 && (n.back() == '/' || n.back() == '\\')) n.pop_back();
    const std::string head = dirname(n);
    if (!head.empty() && head != n && !pyrt::exists(head)) {
        try {
            py_makedirs(head);
        } catch (const OSError& e) {
            if (e.err != EEXIST) throw;
        }
    }
    std::error_code ec;
    std::filesystem::create_directory(fspath(name), ec);
    if (ec && !is_dir(name)) throw OSError(ec.value(), name);
    if (!ec && !is_dir(name)) throw OSError(EEXIST, name);
}
inline void write_text_py(const std::string& path, const std::string& text) {   // _write_text
    try {
        py_makedirs(dirname(abspath(path)));
        pyrt::write_text(path, text);
    } catch (const OSError& e) {
        throw UsageError("cannot write " + path + ": " + e.what());
    }
}
inline std::string universal(const std::string& raw) {   // text-mode newline translation
    std::string t;
    t.reserve(raw.size());
    for (std::size_t k = 0; k < raw.size(); ++k) {
        if (raw[k] == '\r') {
            t += '\n';
            if (k + 1 < raw.size() && raw[k + 1] == '\n') ++k;
        } else {
            t += raw[k];
        }
    }
    return t;
}
// open(path, encoding="utf-8").read(): "" + the missing message when the file is absent, else UsageError for other failures
inline bool read_utf8(const std::string& path, std::string& text) {
    std::string raw;
    try {
        raw = read_bytes(path);
    } catch (const OSError& e) {
        if (e.err == ENOENT) return false;
        throw UsageError("cannot read " + path + ": " + e.what());
    }
    if (const auto u = packtools::console_run::utf8_error(raw)) throw UsageError("cannot read " + path + ": " + *u);
    text = universal(raw);
    return true;
}

inline Val load_config(const std::string& path) {
    std::string text;
    if (!read_utf8(path, text)) throw UsageError("no such file: " + path + " (create one with: padmap.py init)");
    Val data;
    try {
        data = loads(text);
    } catch (const JSONDecodeError& e) {
        const std::string full = e.what();
        const std::size_t k = full.rfind(": line ");
        std::string line = "1";
        if (k != std::string::npos) {
            line.clear();
            for (std::size_t j = k + 7; j < full.size() && std::isdigit(static_cast<unsigned char>(full[j])); ++j) line += full[j];
        }
        throw UsageError(path + " is not valid JSON (line " + line + ": " + e.msg + ")");
    }
    if (data.t != Val::DICT) throw UsageError(path + " must hold a JSON object");
    return data;
}

inline Resolved load_resolved(const Val& a, std::vector<std::string>& errors, std::vector<std::string>& warnings, std::string& source) {
    const std::string cfgp = S(a, "config");
    const std::string layout = S(a, "layout");
    Val config = Val::dict();
    if (!cfgp.empty() && pyrt::exists(cfgp)) {
        config = load_config(cfgp);
        source = packtools::targets::host_basename(cfgp);
    } else if (!cfgp.empty() && cfgp != DEFAULT_CONFIG) {
        throw UsageError("no such file: " + cfgp);
    } else {
        source = "the " + (layout.empty() ? std::string("playstation") : layout) + " preset";
    }
    if (!layout.empty()) {
        Val c2 = Val::dict();
        for (const auto& [k, v] : config.d->items) c2.d->set(k, v);
        c2.d->set("layout", Val::str(layout));
        config = c2;
    }
    return resolve(config, errors, warnings);
}

// ---------------------------------------------------------------- commands
inline bool flag(const Val& a, const std::string& k) { return truthy(get(a, k)); }

inline int cmd_init(const Val& a) {
    const std::string outp = S(a, "out");
    const std::string layout = S(a, "layout");
    if (pyrt::exists(outp) && !flag(a, "force")) throw UsageError(outp + " already exists (pass --force to replace it)");
    write_text_py(outp, dumps2(starter_config(layout)) + "\n");
    out("wrote " + outp + " (layout " + layout + "). Next: padmap.py check --config " + outp);
    return 0;
}

inline int cmd_check(const Val& a) {
    std::vector<std::string> errors, warnings;
    std::string source;
    const Resolved r = load_resolved(a, errors, warnings, source);
    validate(r, errors, warnings);
    Val script_info;
    const std::string script = S(a, "script");
    if (!script.empty()) {
        std::string text;
        if (!read_utf8(script, text)) throw UsageError("no such file: " + script);
        try {
            const auto lines = parse_script(text);
            script_info = dict_of({{"path", Val::str(script)}, {"lines", Val::integer(static_cast<long long>(lines.size()))},
                                   {"last_frame", lines.empty() ? Val() : Val::integer(lines.back().frame)}});
        } catch (const ScriptError& e) {
            errors.push_back("script " + script + ": " + e.what());
        }
    }
    const bool ok = errors.empty() && !(flag(a, "strict") && !warnings.empty());
    if (flag(a, "json")) {
        out(dumps2(dict_of({{"ok", Val::boolean(ok)}, {"source", Val::str(source)}, {"layout", Val::str(r.layout)},
                            {"errors", str_list(errors)}, {"warnings", str_list(warnings)}, {"script", script_info}})));
    } else {
        out("checked " + source + " (layout " + r.layout + ")");
        for (const auto& e : errors) out("  ERROR   " + e);
        for (const auto& w : warnings) out("  warning " + w);
        if (!script_info.is_none()) {
            const Val& lf = get(script_info, "last_frame");
            out("  script " + script + ": " + std::to_string(get(script_info, "lines").i) + " lines, last change at frame " +
                (lf.is_none() ? std::string("None") : std::to_string(lf.i)));
        }
        out(ok ? "  OK: every DS2 button is bound" : "  NOT OK");
    }
    return ok ? 0 : 1;
}

inline int cmd_gen(const Val& a) {
    std::vector<std::string> errors, warnings;
    std::string source;
    const Resolved r = load_resolved(a, errors, warnings, source);
    validate(r, errors, warnings);
    if (!errors.empty()) {
        std::fflush(stdout);
        for (const auto& e : errors) std::fprintf(stderr, "error: %s\n", e.c_str());
        throw UsageError(source + " has " + std::to_string(errors.size()) + " problem(s); fix them (padmap.py check) and run gen again");
    }
    const bool replay = flag(a, "replay");
    const std::string text = generate_cpp(r, replay, source);
    const std::string outp = S(a, "out");
    if (outp == "-") {
        std::fwrite(text.data(), 1, text.size(), stdout);
        return 0;
    }
    write_text_py(outp, text);
    for (const auto& w : warnings) out("warning: " + w);
    out("wrote " + outp + " (" + std::to_string(std::count(text.begin(), text.end(), '\n')) + " lines, layout " + r.layout +
        (replay ? ", input replay + record" : "") + ")");
    out("install: padmap.py install --repo <your PS2Recomp checkout> --from " + outp);
    out("then rebuild incrementally: cmake --build <your build dir>");
    return 0;
}

inline std::string repo_pad_path(const std::string& repo) {
    const std::string path = path_join(path_join(path_join(path_join(repo, "ps2xRuntime"), "src"), "lib"), "ps2_pad.cpp");
    if (!is_file(path)) throw UsageError(repo + " is not a PS2Recomp checkout (no ps2xRuntime/src/lib/ps2_pad.cpp)");
    return path;
}

inline std::string utc_stamp() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char b[32];
    std::strftime(b, sizeof b, "%Y%m%dT%H%M%SZ", &tm);
    return b;
}

inline int cmd_install(const Val& a) {
    const std::string target = repo_pad_path(S(a, "repo"));
    const std::string src = S(a, "source");
    std::string new_text;
    if (!read_utf8(src, new_text)) throw UsageError("no such file: " + src + " (run padmap.py gen first)");
    if (new_text.find(MARKER) == std::string::npos || new_text.find("PSPadBackend::readState") == std::string::npos)
        throw UsageError(src + " was not written by padmap.py gen; refusing to install it");
    const std::string current = text_u(read_bytes(target));
    if (current.find("PSPadBackend::readState") == std::string::npos)
        throw UsageError(target + " does not define PSPadBackend::readState; this checkout's pad layer "
                                  "changed, read it before replacing it");
    const std::string backups = S(a, "backups");
    py_makedirs(backups);
    const std::string orig = path_join(backups, "ps2_pad.cpp.orig");
    if (current.find(MARKER) == std::string::npos && !pyrt::exists(orig)) {
        packtools::targets::copy2(target, orig);
        out("backed up the stock file to " + orig);
    } else {
        const std::string bak = path_join(backups, "ps2_pad.cpp." + utc_stamp() + ".bak");
        packtools::targets::copy2(target, bak);
        out("backed up the current file to " + bak);
    }
    if (current == new_text) {
        out(target + " is already this mapping; nothing to do");
        return 0;
    }
    pyrt::write_text(target, new_text);
    out("installed " + src + " -> " + target);
    out("rebuild incrementally: cmake --build <your build dir> (one .cpp, no headers changed)");
    return 0;
}

inline int cmd_restore(const Val& a) {
    const std::string target = repo_pad_path(S(a, "repo"));
    const std::string orig = path_join(S(a, "backups"), "ps2_pad.cpp.orig");
    if (!is_file(orig)) throw UsageError("no backup at " + orig);
    packtools::targets::copy2(orig, target);
    out("restored the stock ps2_pad.cpp from " + orig);
    return 0;
}

// ======================================================================= CLI
inline int main(const std::vector<std::string>& argv) {
    Parser p(PROG, "Controller mapping for a PS2Recomp port: controls.json in, a drop-in "
                   "ps2xRuntime/src/lib/ps2_pad.cpp out (raylib, DualShock 2 report).");
    p.epilog = "Typical: padmap.py init --layout xbox; edit console/controls.json; "
               "padmap.py check; padmap.py gen --replay; "
               "padmap.py install --repo ~/PS2Recomp";
    p.subcommands("cmd", false); p.sub_metavar = "command";
    auto common = [](Parser& s) {
        s.opt({"--config"}).def(DEFAULT_CONFIG.c_str()).help("mapping file (default console/controls.json; a preset is used if it is missing)");
        s.opt({"--layout"}).choices(layouts()).help("override the layout preset");
        s.opt({"--json"}).store_true().help("machine-readable output");
    };
    { Parser& s = p.sub("init"); s.help_line = "write a starter console/controls.json from a preset";
      s.opt({"--layout"}).choices(layouts()).def("playstation");
      s.opt({"--out"}).def(DEFAULT_CONFIG.c_str());
      s.opt({"--force"}).store_true().help("replace an existing file"); }
    { Parser& s = p.sub("check"); s.help_line = "validate a mapping (every DS2 button bound, no clashes, ranges)";
      common(s);
      s.opt({"--script"}).help("also validate an input replay script");
      s.opt({"--strict"}).store_true().help("warnings fail the check too"); }
    { Parser& s = p.sub("gen"); s.help_line = "write the drop-in ps2_pad.cpp";
      common(s);
      s.opt({"--out"}).def("console/ps2_pad.cpp").help("output file (default console/ps2_pad.cpp, - for stdout)");
      s.opt({"--replay"}).store_true().help("compile in input replay (PS2X_INPUT_SCRIPT) and recording (PS2X_INPUT_RECORD)"); }
    { Parser& s = p.sub("install"); s.help_line = "back up the checkout's ps2_pad.cpp and copy the generated one over it";
      s.opt({"--repo"}).required().help("your PS2Recomp checkout (explicit, never guessed)");
      s.opt({"--from"}).dest("source").def("console/ps2_pad.cpp");
      s.opt({"--backups"}).def("console/backups"); }
    { Parser& s = p.sub("restore"); s.help_line = "put the stock ps2_pad.cpp back from the backup";
      s.opt({"--repo"}).required();
      s.opt({"--backups"}).def("console/backups"); }
    { Parser& s = p.sub("presets"); s.help_line = "print the playstation, xbox, switch and keyboard presets";
      s.pos("name").nargs("?").choices(layouts());
      s.opt({"--json"}).store_true(); }
    { Parser& s = p.sub("table"); s.help_line = "print the DualShock 2 button bit table";
      s.opt({"--json"}).store_true(); }
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd");
    if (cmd.empty()) { std::fflush(stdout); const std::string t = ap::help_text(ap::norm(p, p.prog)); std::fwrite(t.data(), 1, t.size(), stdout); return 2; }
    try {
        if (cmd == "init") return cmd_init(a);
        if (cmd == "check") return cmd_check(a);
        if (cmd == "gen") return cmd_gen(a);
        if (cmd == "install") return cmd_install(a);
        if (cmd == "restore") return cmd_restore(a);
        if (cmd == "presets") {
            const std::string name = S(a, "name");
            print_presets(name.empty() ? layouts() : std::vector<std::string>{name}, flag(a, "json"));
            return 0;
        }
        print_table(flag(a, "json"));
        return 0;
    } catch (const UsageError& e) {
        std::fflush(stdout); std::fprintf(stderr, "error: %s\n", e.what());
        return 2;
    }
}

PACKTOOL("console-controller/padmap", main);
}  // namespace packtools::padmap
