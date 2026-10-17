// XRShell — eingebettete 3×5-Pixelschrift (Tastatur-Overlay #5, VR-Menü).
// Nur Großbuchstaben, Ziffern und wenige Zeichen; Umlaute werden umschrieben.

#pragma once

#include <cstdint>

namespace pixelfont {

// --- 3×5-Pixelfont (öffentliches LED-Font-Muster) ------------------------------
// 5 Bytes pro Glyphe, je Byte 3 Pixel (Bit 2 = links … Bit 0 = rechts).
inline constexpr uint8_t kFont[][5] = {
    /*A*/ {0b010, 0b101, 0b111, 0b101, 0b101},
    /*B*/ {0b110, 0b101, 0b110, 0b101, 0b110},
    /*C*/ {0b011, 0b100, 0b100, 0b100, 0b011},
    /*D*/ {0b110, 0b101, 0b101, 0b101, 0b110},
    /*E*/ {0b111, 0b100, 0b110, 0b100, 0b111},
    /*F*/ {0b111, 0b100, 0b110, 0b100, 0b100},
    /*G*/ {0b011, 0b100, 0b101, 0b101, 0b011},
    /*H*/ {0b101, 0b101, 0b111, 0b101, 0b101},
    /*I*/ {0b111, 0b010, 0b010, 0b010, 0b111},
    /*J*/ {0b001, 0b001, 0b001, 0b101, 0b010},
    /*K*/ {0b101, 0b101, 0b110, 0b101, 0b101},
    /*L*/ {0b100, 0b100, 0b100, 0b100, 0b111},
    /*M*/ {0b101, 0b111, 0b111, 0b101, 0b101},
    /*N*/ {0b101, 0b110, 0b101, 0b101, 0b101},
    /*O*/ {0b010, 0b101, 0b101, 0b101, 0b010},
    /*P*/ {0b110, 0b101, 0b110, 0b100, 0b100},
    /*Q*/ {0b010, 0b101, 0b101, 0b010, 0b001},
    /*R*/ {0b110, 0b101, 0b110, 0b101, 0b101},
    /*S*/ {0b011, 0b100, 0b010, 0b001, 0b110},
    /*T*/ {0b111, 0b010, 0b010, 0b010, 0b010},
    /*U*/ {0b101, 0b101, 0b101, 0b101, 0b111},
    /*V*/ {0b101, 0b101, 0b101, 0b101, 0b010},
    /*W*/ {0b101, 0b101, 0b101, 0b111, 0b101},
    /*X*/ {0b101, 0b101, 0b010, 0b101, 0b101},
    /*Y*/ {0b101, 0b101, 0b010, 0b010, 0b010},
    /*Z*/ {0b111, 0b001, 0b010, 0b100, 0b111},
    /*0*/ {0b111, 0b101, 0b101, 0b101, 0b111},
    /*1*/ {0b010, 0b110, 0b010, 0b010, 0b111},
    /*2*/ {0b111, 0b001, 0b111, 0b100, 0b111},
    /*3*/ {0b111, 0b001, 0b111, 0b001, 0b111},
    /*4*/ {0b101, 0b101, 0b111, 0b001, 0b001},
    /*5*/ {0b111, 0b100, 0b111, 0b001, 0b111},
    /*6*/ {0b111, 0b100, 0b111, 0b101, 0b111},
    /*7*/ {0b111, 0b001, 0b010, 0b010, 0b010},
    /*8*/ {0b111, 0b101, 0b111, 0b101, 0b111},
    /*9*/ {0b111, 0b101, 0b111, 0b001, 0b111},
    /*-*/ {0b000, 0b000, 0b111, 0b000, 0b000},
    /*=*/ {0b000, 0b111, 0b000, 0b111, 0b000},
    /*,*/ {0b000, 0b000, 0b000, 0b010, 0b100},
    /*.*/ {0b000, 0b000, 0b000, 0b000, 0b010},
    /*:*/ {0b000, 0b010, 0b000, 0b010, 0b000},
    /*<*/ {0b001, 0b010, 0b100, 0b010, 0b001},
    /*>*/ {0b100, 0b010, 0b001, 0b010, 0b100},
    /*^*/ {0b010, 0b111, 0b010, 0b010, 0b010},
    /*%*/ {0b101, 0b001, 0b010, 0b100, 0b101},
    /*+*/ {0b000, 0b010, 0b111, 0b010, 0b000},
    /*[*/ {0b110, 0b100, 0b100, 0b100, 0b110},
    /*]*/ {0b011, 0b001, 0b001, 0b001, 0b011},
    /*;*/ {0b000, 0b010, 0b000, 0b010, 0b100},
    /*'*/ {0b010, 0b010, 0b000, 0b000, 0b000},
    /*/*/ {0b001, 0b001, 0b010, 0b100, 0b100},
    /*\*/ {0b100, 0b100, 0b010, 0b001, 0b001},
    /*#*/ {0b101, 0b111, 0b101, 0b111, 0b101},
};

inline const uint8_t* glyphFor(char c) {
    if (c >= 'A' && c <= 'Z') return kFont[c - 'A'];
    if (c >= '0' && c <= '9') return kFont[26 + c - '0'];
    switch (c) {
    case '-': return kFont[36];
    case '=': return kFont[37];
    case ',': return kFont[38];
    case '.': return kFont[39];
    case ':': return kFont[40];
    case '<': return kFont[41];
    case '>': return kFont[42];
    case '^': return kFont[43];
    case '%': return kFont[44];
    case '+': return kFont[45];
    case '[': return kFont[46];
    case ']': return kFont[47];
    case ';': return kFont[48];
    case '\'': return kFont[49];
    case '/': return kFont[50];
    case '\\': return kFont[51];
    case '#': return kFont[52];
    default:  return nullptr;   // Leerzeichen etc.
    }
}

} // namespace pixelfont
