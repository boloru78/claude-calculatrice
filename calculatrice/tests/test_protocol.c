/* test_protocol.c — Dialogue avec l'ESP32 (protocol.c). */
#include "test.h"
#include "protocol.h"
#include <string.h>

static void check_final(void)
{
    CHECK(protocol_is_final("FIN"));
    CHECK(protocol_is_final("OK"));
    CHECK(protocol_is_final("PONG -60 12:00:00"));
    CHECK(protocol_is_final("ERR Pas de Wi-Fi."));
    CHECK(!protocol_is_final("ATT"));
    CHECK(!protocol_is_final("R FIN"));
    CHECK(!protocol_is_final("R OK"));
    CHECK(!protocol_is_final(""));
}

static void check_question(void)
{
    char line[32];
    protocol_question(line, sizeof line, "Salut");
    CHECK(!strcmp(line, "Q Salut"));
    /* Une seule ligne : les retours à la ligne deviennent des espaces. */
    protocol_question(line, sizeof line, "a\nb\rc");
    CHECK(!strcmp(line, "Q a b c"));
    /* Tronquée sans couper un caractère UTF-8 (« é » fait 2 octets). */
    char small[6];
    protocol_question(small, sizeof small, "aééé");
    CHECK(!strcmp(small, "Q aé"));
}

static void check_answer(void)
{
    char text[64];
    reply_t r = protocol_parse("ATT\nR Bonjour !\nR Deuxième ligne\nFIN",
        text, sizeof text);
    CHECK(r.kind == REPLY_ANSWER);
    CHECK(!strcmp(text, "Bonjour !\nDeuxième ligne"));

    /* Lignes vides de la réponse, et lignes inconnues ignorées. */
    r = protocol_parse("ATT\nR a\nR \nbizarre\nR b\nFIN", text, sizeof text);
    CHECK(r.kind == REPLY_ANSWER);
    CHECK(!strcmp(text, "a\n\nb"));

    /* Réponse trop longue : tronquée proprement. */
    char small[8];
    r = protocol_parse("R abcdefghijkl\nFIN", small, sizeof small);
    CHECK(r.kind == REPLY_ANSWER);
    CHECK(!strcmp(small, "abcdefg"));

    r = protocol_parse("ATT\nERR Mac injoignable (serveur lancé ?)", text,
        sizeof text);
    CHECK(r.kind == REPLY_ERROR);
    CHECK(!strcmp(text, "Mac injoignable (serveur lancé ?)"));

    r = protocol_parse("OK", text, sizeof text);
    CHECK(r.kind == REPLY_OK);
    r = protocol_parse("ATT\nR sans fin", text, sizeof text);
    CHECK(r.kind == REPLY_INVALID);
    r = protocol_parse("", text, sizeof text);
    CHECK(r.kind == REPLY_INVALID);
}

static void check_status(void)
{
    char text[8];
    reply_t r = protocol_parse("PONG -63 14:03:27", text, sizeof text);
    CHECK(r.kind == REPLY_STATUS);
    CHECK(r.wifi && r.rssi == -63);
    CHECK(r.has_time && r.hours == 14 && r.minutes == 3 && r.seconds == 27);

    r = protocol_parse("PONG - 08:00:00", text, sizeof text);
    CHECK(r.kind == REPLY_STATUS && !r.wifi && r.has_time);
    r = protocol_parse("PONG - -", text, sizeof text);
    CHECK(r.kind == REPLY_STATUS && !r.wifi && !r.has_time);
    /* Valeurs absurdes : ignorées. */
    r = protocol_parse("PONG 12 25:00:00", text, sizeof text);
    CHECK(!r.wifi && !r.has_time);
    r = protocol_parse("PONG -60x 1:2", text, sizeof text);
    CHECK(!r.wifi && !r.has_time);

    CHECK(protocol_wifi_bars(-40) == 4);
    CHECK(protocol_wifi_bars(-60) == 3);
    CHECK(protocol_wifi_bars(-70) == 2);
    CHECK(protocol_wifi_bars(-80) == 1);
    CHECK(protocol_wifi_bars(-95) == 0);
}

void test_protocol(void)
{
    check_final();
    check_question();
    check_answer();
    check_status();
}
