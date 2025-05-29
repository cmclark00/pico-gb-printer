#include "../include/char_encode.h" // Adjusted include path
#include <string.h> // For memset if we use it for padding

char pokemon_char_to_encoded(int byte) {
    switch(byte) {
    case ' ': return SPACE_;
    case 'A': return A_;
    case 'B': return B_;
    case 'C': return C_;
    case 'D': return D_;
    case 'E': return E_;
    case 'F': return F_;
    case 'G': return G_;
    case 'H': return H_;
    case 'I': return I_;
    case 'J': return J_;
    case 'K': return K_;
    case 'L': return L_;
    case 'M': return M_;
    case 'N': return N_;
    case 'O': return O_;
    case 'P': return P_;
    case 'Q': return Q_;
    case 'R': return R_;
    case 'S': return S_;
    case 'T': return T_;
    case 'U': return U_;
    case 'V': return V_;
    case 'W': return W_;
    case 'X': return X_;
    case 'Y': return Y_;
    case 'Z': return Z_;
    case '(': return O_PAREN_;
    case ')': return C_PAREN_;
    case ':': return COLON_;
    case ';': return SEMI_;
    case '[': return O_BRACKET_;
    case ']': return C_BRACKET_;
    case 'a': return a_;
    case 'b': return b_;
    case 'c': return c_;
    case 'd': return d_;
    case 'e': return e_;
    case 'f': return f_;
    case 'g': return g_;
    case 'h': return h_;
    case 'i': return i_;
    case 'j': return j_;
    case 'k': return k_;
    case 'l': return l_;
    case 'm': return m_;
    case 'n': return n_;
    case 'o': return o_;
    case 'p': return p_;
    case 'q': return q_;
    case 'r': return r_;
    case 's': return s_;
    case 't': return t_;
    case 'u': return u_;
    case 'v': return v_;
    case 'w': return w_;
    case 'x': return x_;
    case 'y': return y_;
    case 'z': return z_;
    // case 'é': return e_ACCENT_; // Special char, handle with care
    case '\'': return S_QUOTE_; // Apostrophe
    // PK and MN are special symbols, not direct char mappings
    case '-': return DASH_;
    case '?': return QUESTION_;
    case '!': return EXCLAIM_;
    case '.': return PERIOD_;
    // Arrows, MALE, FEMALE are special symbols
    case '0': return _0_;
    case '1': return _1_;
    case '2': return _2_;
    case '3': return _3_;
    case '4': return _4_;
    case '5': return _5_;
    case '6': return _6_;
    case '7': return _7_;
    case '8': return _8_;
    case '9': return _9_;
    // Extended Chars from Flipper source (using octal representations for safety)
    // These might need specific input methods if you intend for users to type them.
    case '\351': /* é */ return e_ACCENT_;
    // The Flipper code had MALE as \201 and FEMALE as \200, these are not standard ASCII/extended ASCII
    // mapping directly to single chars. Game Boy likely has specific codes for these.
    // For now, stick to more common ASCII mappings. If specific GB symbols needed, they must be input differently.

    default:
        return SPACE_; // Return space for unknown chars, or TERM_ if preferred
    }
}

int pokemon_encoded_to_char(char byte) {
    switch((unsigned char)byte) { // Cast to unsigned char for switch on 0x80+ values
    case SPACE_: return ' ';
    case A_: return 'A';
    case B_: return 'B';
    case C_: return 'C';
    case D_: return 'D';
    case E_: return 'E';
    case F_: return 'F';
    case G_: return 'G';
    case H_: return 'H';
    case I_: return 'I';
    case J_: return 'J';
    case K_: return 'K';
    case L_: return 'L';
    case M_: return 'M';
    case N_: return 'N';
    case O_: return 'O';
    case P_: return 'P';
    case Q_: return 'Q';
    case R_: return 'R';
    case S_: return 'S';
    case T_: return 'T';
    case U_: return 'U';
    case V_: return 'V';
    case W_: return 'W';
    case X_: return 'X';
    case Y_: return 'Y';
    case Z_: return 'Z';
    case O_PAREN_: return '(';
    case C_PAREN_: return ')';
    case COLON_: return ':';
    case SEMI_: return ';';
    case O_BRACKET_: return '[';
    case C_BRACKET_: return ']';
    case a_: return 'a';
    case b_: return 'b';
    case c_: return 'c';
    case d_: return 'd';
    case e_: return 'e';
    case f_: return 'f';
    case g_: return 'g';
    case h_: return 'h';
    case i_: return 'i';
    case j_: return 'j';
    case k_: return 'k';
    case l_: return 'l';
    case m_: return 'm';
    case n_: return 'n';
    case o_: return 'o';
    case p_: return 'p';
    case q_: return 'q';
    case r_: return 'r';
    case s_: return 's';
    case t_: return 't';
    case u_: return 'u';
    case v_: return 'v';
    case w_: return 'w';
    case x_: return 'x';
    case y_: return 'y';
    case z_: return 'z';
    case e_ACCENT_: return '\351'; // é
    case S_QUOTE_: return '\'';
    // PK_, MN_ special symbols
    case DASH_: return '-';
    // r_TICK_, m_TICK_ special
    case QUESTION_: return '?';
    case EXCLAIM_: return '!';
    case PERIOD_: return '.';
    // R_ARR_, D_ARR_ special
    case MALE_: return 'M'; // Placeholder, real char depends on font/system
    case FEMALE_: return 'F'; // Placeholder
    case _0_: return '0';
    case _1_: return '1';
    case _2_: return '2';
    case _3_: return '3';
    case _4_: return '4';
    case _5_: return '5';
    case _6_: return '6';
    case _7_: return '7';
    case _8_: return '8';
    case _9_: return '9';
    case TERM_: return '\0'; // Convert TERM_ to null terminator for C strings
    default:
        return '?'; // Return question mark for unmapped encoded chars
    }
}

void pokemon_str_to_encoded_array(uint8_t* dest, const char* src, size_t n, bool pad_with_term_IGNORED) {
    // pad_with_term_IGNORED is kept for signature compatibility.
    // The logic is to encode src, then pad with TERM_ up to n bytes.
    // The last byte dest[n-1] will always be TERM_ if n > 0.

    if (n == 0) return;

    size_t i = 0;
    // Encode the source string
    if (src) { // Check if src is not NULL
        for (i = 0; i < n && src[i] != '\0'; i++) {
            dest[i] = pokemon_char_to_encoded(src[i]);
        }
    }
    
    // Fill the rest of dest with TERM_ (0x50)
    // This includes the byte immediately after the content if the string was shorter than n,
    // and all subsequent bytes up to n.
    for (; i < n; i++) {
        dest[i] = TERM_;
    }
    // Ensure the very last character is TERM_ if the field has any length.
    // This is mostly redundant given the loop above but acts as a safeguard.
    // if (n > 0) { 
    //     dest[n-1] = TERM_;
    // }
}

void pokemon_encoded_array_to_str(char* dest, const uint8_t* src, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) {
        dest[i] = pokemon_encoded_to_char(src[i]);
        if (dest[i] == '\0') { // Stop if we hit a null terminator (from TERM_)
            break;
        }
    }
    // Ensure null termination if we filled up to n without hitting TERM_
    if (i == n) {
        dest[i-1] = '\0'; 
    }
}

void pokemon_encoded_array_to_str_until_terminator(char* dest, const uint8_t* src, size_t max_len) {
    size_t i;
    for (i = 0; i < max_len -1; i++) { // -1 to ensure space for null terminator
        if (src[i] == TERM_) {
            break;
        }
        dest[i] = pokemon_encoded_to_char(src[i]);
    }
    dest[i] = '\0'; // Null terminate
} 