#if 0
    #!/bin/bash
    if [[ "$OSTYPE" == "linux-gnu" ]]; then
        SDL2_CFLAGS=`pkg-config --cflags sdl2`
        SDL2_LDFLAGS=`pkg-config --libs sdl2`
        CFLAGS="-g -w $SDL2_CFLAGS -o run"
        LDFLAGS="$SDL2_LDFLAGS -lGL -lm imgui.a"
    elif [[ "$OSTYPE" == "darwin15" ]]; then
        SDL2_CFLAGS="-I/Library/Frameworks/SDL2.framework/Headers"
        SDL2_LDFLAGS="-framework SDL2"
        CFLAGS="-g -w $SDL2_CFLAGS -o run"
        LDFLAGS="$SDL2_LDFLAGS -framework OpenGL -lm imgui.a"
    fi

    if [ ! -f imgui.a ]; then
        c++ -O2 $SDL2_CFLAGS -c imgui.cpp imgui_draw.cpp imgui_impl_sdl.cpp
        ar rcs imgui.a imgui.o imgui_draw.o imgui_impl_sdl.o
    fi
    c++ $CFLAGS sdl_main.cpp $LDFLAGS
    exit
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <SDL.h>
#if __APPLE__
#include <opengl/gl.h>
#else
#include <GL/gl.h>
#endif
#include "imgui.h"
#include "imgui_impl_sdl.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
    
typedef int heletal32;
typedef unsigned char naturlig8;
typedef float reelle32;
typedef heletal32 h32;
typedef naturlig8 n8;
typedef reelle32 r32;

#define PI M_PI
#define PATH_SIZE 4000

// Vores strukturer start
struct billede {
    char sti[256];
    h32 w, h, c;
    n8 *ptr;
};

struct grayscale {
    h32 w, h;
    n8 *ptr;
};

struct texture {
    h32 w, h;
    GLuint id;
};

struct {
    char billedefil[PATH_SIZE];
    bool billedefil_notfound,
         tegn_grayscale,
         besked_notfound,
         har_aendringer,
         vis_aendringer,
         kunne_ikke_gemmes;
    char *gemme_sti;
    h32 *vores_besked, vores_besked_max, gemme_sti_max;

} gui_data = {0};

// Vores strukturer slut

// Globale variable start
SDL_Window *win;
SDL_GLContext gl_ctx;
bool running = true, er_billede_aaben = false;
struct texture aaben_tex = {0}, aaben_tex_grayscale = {0}, grayscale_merke_tex = {0}, grayscale_aendringer_tex = {0};
struct billede aaben_billede;
struct grayscale aaben_grayscale;
struct grayscale grayscale_merke;
struct grayscale grayscale_aendringer;
h32 modulus, dimensioner, n_cols, n_rows;
h32 hamming[2 * 6] = {
    0, 1,
    1, 0,
    1, 1,
    1, 2,
    1, 3,
    1, 4
};
h32 huffman_vals[] = {
    0b001, 0b1101, 0b1100, 0b1011, 0b1001, 0b1000, 0b0111,
    0b0100, 0b11111, 0b11101, 0b10101, 0b01101, 0b01100, 0b01011,
    0b01010
};
char huffman_keys[] = {
    'E', 'A', 'R', 'I', 'O', 'T', 'N',
    'S', 'L', 'C', 'U', 'D', 'P', 'M',
    'H', 'G', 'B', 'F', 'Y', 'W', 'K',
    'V', 'X', 'Z', 'Q', 'J'
};
// Globale variable slut

// Funktioner start
bool
abn_billede_fra_filsystem(char *sti, struct billede *ud) {
    if (strlen(sti) > PATH_SIZE - 1) {
        fprintf(stderr, "Stien for billedefil '%s' er for lang (max er %d)\n", sti, PATH_SIZE - 1);
        return false;
    }
    strcpy(ud->sti, sti);
    ud->ptr = stbi_load(sti, &ud->w, &ud->h, &ud->c, 0);
    if (!ud->ptr) {
        fprintf(stderr, "Kunne ikke abne %s\n", sti);
        return false;
    }

    printf("Fil '%s' abnet med bredde = %d, hojde = %d og antal farver = %d\n", sti, ud->w, ud->h, ud->c);

    return true;
}

bool
convert_to_grayscale(n8 *ptr, h32 w, h32 h, h32 c, struct grayscale *ud) {
    n8 r, g, b, grayscale;
    h32 i, n, v, j;
    r32 p;

    for (j = i = 0, n = w * h * c; i < n; i += c, ++j) {
        r = ptr[i + 0];
        g = ptr[i + 1];
        b = ptr[i + 2];

        v = r + g + b;
        p = (r32)v / (3.0f);
        grayscale = (n8)p;

        ud->ptr[j] = grayscale;
    }

    return true;
}

bool
upload_billede_til_gpu(n8 *ptr, h32 w, h32 h, h32 c, struct texture *ud) {
    GLenum internal_format, format;

    if (c == 3) {
        format = GL_RGB;
        internal_format = GL_RGB;
    } else if (c == 4) {
        format = GL_RGBA;
        internal_format = GL_RGB;
    } else if (c == 1) {
        format = GL_LUMINANCE;
        internal_format = GL_LUMINANCE;
    }
    if (ud->id == 0) {
        glGenTextures(1, &ud->id);
        if (ud->id == 0) {
            printf("Kunne ikke skabe en ny texture\n");
            return false;
        }
    }
    glBindTexture(GL_TEXTURE_2D, ud->id);
    glTexImage2D(GL_TEXTURE_2D, 0, internal_format, w, h, 0, format, GL_UNSIGNED_BYTE, ptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    ud->w = w; ud->h = h;

    return true;
}

void
matrix_multiplikation(h32 *a, h32 a_r, h32 a_c, h32 *b, h32 b_r, h32 b_c, h32 *out) {
    h32 m, n, q, k, i, j;
    m = a_r;
    k = a_c;
    n = b_c;

    for (i = 0; i < m; i = i + 1 ) {
        for ( j = 0; j < n; j = j + 1 ) {
            out[ j * 2 + i ] = 0;
            for (q = 0; q < k; q = q + 1) {
                out[ j * 2 + i] = out[ j * 2 + i] + a[ q * 2 + i] * b[ j * 2 + q];
            }
        }
    }
}

h32
hamming_lookup(h32 *H, h32 *s) {
    h32 i, r, c;

    r = 2;
    c = 6;

    for (i = 0; i < c; ++i) {
        if (H[r * i + 0] == s[0] &&
                H[r * i + 1] == s[1])
            return i;
    }

    return -1;
}

h32
farve_funktion(h32 *v, h32 n) {
    h32 rv, i;

    rv = 0;
    for (i = 1; i <= n; ++i) {
        rv += v[i - 1] * i;
    }

    return rv % 5;
}

bool
encode_once(h32 *H, h32 *v, h32 *s_) {
    h32 s[2], d[2], x;
    h32 idx, i;
    div_t dv;

    matrix_multiplikation(H, 2, 6, v, 6, 1, s);
    d[0] = s[0] % 5;
    d[1] = s[1] % 5;
    d[0] = s_[0] - d[0];
    d[1] = s_[1] - d[1];

    if (d[0] < 0)
        d[0] = 5 + d[0];
    if (d[1] < 0)
        d[1] = 5 + d[1];

    if (d[0] == 0 && d[1] == 0) {
        return true;
    }


    x = d[0];
    if (x == 0) {
        x = d[1];
        d[1] /= x;
    }
    else {
        for (i = 0; i < 5; ++i) {
            if ((x * i) % 5 == d[1])
                break;
        }
        assert(i < 5);
        d[0] = 1;
        d[1] = i;
    }

    assert(d[0] < 5 && d[1] < 5);
    
    idx = hamming_lookup(H, d);
    if (idx < 0) {
        fprintf(stderr, "Unable to find (%d, %d) in Hammingmatrix\n", d[0], d[1]);
        return false;
    }

    v[idx] = (v[idx] + x) % 5;
    matrix_multiplikation(H, 2, 6, v, 6, 1, s);
    s[0] %= 5;
    s[1] %= 5;

    assert(s_[0] == s[0] && s_[1] == s[1]);

    return true;
}

bool
vores_steganografi_funktion(struct grayscale in, struct grayscale *out, h32 *msg) {
    h32 i, j, k, max, cnt, mod,
        v1[6], v2[6], d[6], s_[2], inc,
        msg_sz, q, r, indent;
    r32 *LSB;
    n8 byte, byte_;
    div_t dv;

    inc = 1;
    mod = modulus;
    msg_sz = 6;
    max = 12 * (msg_sz / 2);

    memcpy(out->ptr, in.ptr, in.w * in.h);

    memset(grayscale_aendringer.ptr, 0x80, in.w * in.h);

    for (cnt = 0, i = 0; i < max; i += 12, cnt += 2) {
        for (j = 0; j < 6; ++j) {
            s_[0] = out->ptr[i + j * 2];
            s_[1] = out->ptr[i + j * 2 + 1];
            v1[j] = v2[j] = farve_funktion(s_, 2);
        }
        s_[0] = msg[cnt]; s_[1] = msg[cnt + 1];
        if (!encode_once(hamming, v2, s_)) {
            return false;
        }

        for (j = 0; j < 6; ++j) {
            k = (v2[j] - v1[j]);
            if (k < 0) {
                k = mod + k;
            }

            if (k == 1) {
                out->ptr[i + j * 2] += inc;
                grayscale_aendringer.ptr[i + j * 2] = 0xFF;
                // Right
            } else if (k == 2) {
                out->ptr[i + j * 2 + 1] += inc;
                grayscale_aendringer.ptr[i + j * 2 + 1] = 0xFF;
                // UP
            } else if (k == 3) {
                out->ptr[i + j * 2 + 1] -= inc;
                grayscale_aendringer.ptr[i + j * 2 + 1] = 0x00;
                // Down
            } else if (k == 4) {
                out->ptr[i + j * 2] -= inc;
                grayscale_aendringer.ptr[i + j * 2] = 0x00;
                // left
            }
        }
    }

    upload_billede_til_gpu(grayscale_aendringer.ptr, grayscale_aendringer.w, grayscale_aendringer.h, 1, &grayscale_aendringer_tex);
    gui_data.har_aendringer = true;

    return true;
}

bool
vores_laese_funktion(struct grayscale in, h32 *msg) {
    h32 i, j, k, cnt, max, v[6], s_[2], msg_sz, indent;
    n8 byte, byte_;

    msg_sz = 6;
    max = 12 * (msg_sz / 2);

    k = 0;
    for (cnt = i = 0; i < max; i += 12, cnt += 2) {
        for (j = 0;  j < 6; ++j) {
            s_[0] = in.ptr[i + j * 2];
            s_[1] = in.ptr[i + j * 2 + 1];
            v[j] = farve_funktion(s_, 2);
        }

        matrix_multiplikation(hamming, 2, 6, v, 6, 1, s_);
        s_[0] %= 5;
        s_[1] %= 5;

        msg[cnt] = s_[0];
        msg[cnt + 1] = s_[1];
    }

    return true;
}

// Funktioner slut

int main() {
    // Lokale variable start
    r32 x, y, v, aspect, aspect_billede;
    h32 i, j, k, num_it;
    h32 ventetid, sidste_tid;
    bool flip, rv;
    SDL_Event ev;

    // Lokale variable slut

    // Initialisation start
    if (SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL init failed\n");
        return EXIT_FAILURE;
    }

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);

    win = SDL_CreateWindow("P2 Software", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!win) {
        fprintf(stderr, "Failed to create window\n");
        return EXIT_FAILURE;
    }

    gl_ctx = SDL_GL_CreateContext(win);
    if (!gl_ctx) {
        fprintf(stderr, "Failed to create GL context\n");
        return EXIT_FAILURE;
    }

    ImGui_ImplSdl_Init(win);

    gui_data.vores_besked_max = 6;
    gui_data.vores_besked = (h32 *)calloc(gui_data.vores_besked_max, sizeof *gui_data.vores_besked);
    if (!gui_data.vores_besked) {
        printf("Unable to allocate memory\n");
        return false;
    }

    gui_data.gemme_sti_max = 1024 * 1024;
    gui_data.gemme_sti = (char *)calloc(gui_data.gemme_sti_max, 1);
    if (!gui_data.gemme_sti) {
        printf("Unable to allocate memory\n");
        return false;
    }

    modulus = 5;
    dimensioner = 2;
    n_rows = 2;
    n_cols = 6;

    strcpy(gui_data.billedefil, "../serveimage.jpe");
    strcpy(gui_data.gemme_sti, "illuminati.png");
    // Initialisation slut
    
    while (running) {
        while (SDL_PollEvent(&ev)) {
            ImGui_ImplSdl_ProcessEvent(&ev);
            if (ev.type == SDL_QUIT) {
                running = false;
            }

        }
        ImGui_ImplSdl_NewFrame(win);

        // Game loop start

        ImGui::Begin("Værktøjskasse");

        if (gui_data.billedefil_notfound)
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
        ImGui::InputText("Billedefil", gui_data.billedefil, PATH_SIZE);
        if (gui_data.billedefil_notfound)
            ImGui::PopStyleColor();
        if (ImGui::Button("åbn billedefil")) {
            if (abn_billede_fra_filsystem(gui_data.billedefil, &aaben_billede)) {
                aaben_grayscale.w = aaben_billede.w;
                aaben_grayscale.h = aaben_billede.h;
                aaben_grayscale.ptr = (n8*)calloc(aaben_grayscale.w * aaben_grayscale.h, 1);

                grayscale_aendringer.w = aaben_billede.w;
                grayscale_aendringer.h = aaben_billede.h;
                grayscale_aendringer.ptr = (n8*)calloc(aaben_grayscale.w * aaben_grayscale.h, 1);

                gui_data.billedefil_notfound = false;
                if (aaben_billede.c > 1)
                    rv = convert_to_grayscale(aaben_billede.ptr, aaben_billede.w, aaben_billede.h, aaben_billede.c, &aaben_grayscale);
                else {
                    memcpy(aaben_grayscale.ptr, aaben_billede.ptr, aaben_grayscale.w * aaben_grayscale.h);
                    rv = true;
                }

                grayscale_merke = aaben_grayscale;
                grayscale_merke.ptr = (n8*)calloc(grayscale_merke.h * grayscale_merke.w, 1);

                if (rv) {
                    if (upload_billede_til_gpu(aaben_grayscale.ptr, aaben_grayscale.w, aaben_grayscale.h, 1, &aaben_tex_grayscale) &&
                            upload_billede_til_gpu(aaben_billede.ptr, aaben_billede.w, aaben_billede.h, aaben_billede.c, &aaben_tex)) {
                        er_billede_aaben = true;
                    } else 
                        return EXIT_FAILURE;
                } else
                    return EXIT_FAILURE;
            } else
                gui_data.billedefil_notfound = true;

        }

        ImGui::Checkbox("Tegn grayscale", &gui_data.tegn_grayscale);
        bool temp = false;
        ImGui::Checkbox("Tegn aendringer", (gui_data.har_aendringer ? &gui_data.vis_aendringer : &temp));
        ImGui::Separator();
        ImGui::Text("Hamming Matrix");
        ImGui::Columns(n_cols);
            for (i = 0; i < n_cols; ++i) {
                ImGui::InputInt("", &hamming[i * 2], 0, modulus);
                ImGui::NextColumn();
            }
            ImGui::Separator();
            for (i = 0; i < n_cols; ++i) {
                ImGui::InputInt("", &hamming[i * 2 + 1], 0, modulus);
                ImGui::NextColumn();
            }
        ImGui::Columns(1);
        ImGui::Separator();

        if (gui_data.besked_notfound)
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));

        ImGui::Text("Besked");
        ImGui::Columns(3);
        for (i = 0; i < 6; i += 2) {
            j = gui_data.vores_besked[i];
            k = gui_data.vores_besked[i + 1];
            ImGui::PushID(i);
            ImGui::InputInt("##value", &j, 1);
            ImGui::PopID();
            ImGui::PushID(i + 1);
            ImGui::InputInt("##value", &k, 1);
            ImGui::PopID();
            if (j < 0)
                j = 5 + j;
            if (k < 0)
                k = 5 + k;
            gui_data.vores_besked[i] = j % modulus;
            gui_data.vores_besked[i + 1] = k % modulus;
            ImGui::NextColumn();
        }
        ImGui::Separator();

        if (gui_data.besked_notfound)
            ImGui::PopStyleColor();
        if (ImGui::Button("Steganografiser")) {
            if (er_billede_aaben) {
                if (vores_steganografi_funktion(aaben_grayscale, &grayscale_merke, gui_data.vores_besked)) {
                    if (upload_billede_til_gpu(grayscale_merke.ptr, grayscale_merke.w, grayscale_merke.h, 1, &grayscale_merke_tex)) {
                    } else
                        return EXIT_FAILURE;
                } else
                    return EXIT_FAILURE;
            }
        }

        if (ImGui::Button("Find besked")) {
            if (er_billede_aaben) {
                if (vores_laese_funktion(grayscale_merke, gui_data.vores_besked)) {
                } else
                    gui_data.besked_notfound = true;
            }
        }

        ImGui::Separator();
        if (gui_data.kunne_ikke_gemmes)
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
        ImGui::InputText("Filsti", gui_data.gemme_sti, gui_data.gemme_sti_max);
        if (gui_data.kunne_ikke_gemmes)
            ImGui::PopStyleColor();
        if (ImGui::Button("Gem")) {
            if (stbi_write_png(gui_data.gemme_sti, grayscale_merke.w, grayscale_merke.h, 1, grayscale_merke.ptr, grayscale_merke.w)) {
            } else
                gui_data.kunne_ikke_gemmes = true;
        }
        ImGui::End();


        glViewport(0, 0, (int)ImGui::GetIO().DisplaySize.x, (int)ImGui::GetIO().DisplaySize.y);
        aspect = (r32)ImGui::GetIO().DisplaySize.y / (r32)ImGui::GetIO().DisplaySize.x;
        glClear(GL_COLOR_BUFFER_BIT);

        if (er_billede_aaben) {
            aspect_billede = aaben_tex.h / aaben_tex.w;
            glEnable(GL_TEXTURE_2D);
            if (gui_data.tegn_grayscale)
                glBindTexture(GL_TEXTURE_2D, aaben_tex_grayscale.id);
            else
                glBindTexture(GL_TEXTURE_2D, aaben_tex.id);
            glBegin(GL_QUADS);
                glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
                glTexCoord2f(0.0f, 0.0f); glVertex2f(-1.0, 1.0);
                glTexCoord2f(1.0f, 0.0f); glVertex2f(0.0, 1.0);
                glTexCoord2f(1.0f, 1.0f); glVertex2f(0.0, -1.0);
                glTexCoord2f(0.0f, 1.0f); glVertex2f(-1.0, -1.0);
            glEnd();

            if (gui_data.vis_aendringer)
                glBindTexture(GL_TEXTURE_2D, grayscale_aendringer_tex.id);
            else
                glBindTexture(GL_TEXTURE_2D, grayscale_merke_tex.id);
            glBegin(GL_QUADS);
                glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
                glTexCoord2f(0.0f, 0.0f); glVertex2f(0.0, 1.0);
                glTexCoord2f(1.0f, 0.0f); glVertex2f(1.0, 1.0);
                glTexCoord2f(1.0f, 1.0f); glVertex2f(1.0, -1.0);
                glTexCoord2f(0.0f, 1.0f); glVertex2f(0.0, -1.0);
            glEnd();

            glDisable(GL_TEXTURE_2D);
            glBegin(GL_LINES);
                glColor4f(0.0f, 0.0f, 0.0f, 1.0f);
                glVertex2f(0.0f, -1.0f);
                glVertex2f(0.0f, 1.0f);
            glEnd();
        }

        // Game loop slut
        ImGui::Render();
        SDL_GL_SwapWindow(win);
    }

    return EXIT_SUCCESS;
}

// vim: tabstop=4 shiftwidth=4 expandtab
