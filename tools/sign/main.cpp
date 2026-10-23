// quark_sign: release-package signing (docs/SIGNING.md).
//   quark_sign keygen                      print a new private key and its public key
//   quark_sign pubkey                      private key on stdin (or $QUARK_SIGNING_KEY) -> public key
//   quark_sign sign <file>                 private key from $QUARK_SIGNING_KEY; writes <file>.sig
//   quark_sign verify <file> [pubkey]      checks <file>.sig against pubkey (default: the built-in release key)
//   quark_sign sign-master <file>          like sign, but over sign::kMasterSignPrefix + the file (site/master.txt)
//   quark_sign verify-master <file> [pubkey]
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include "common/sign.h"

static bool readAll(const std::string& path, std::string* out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    *out = ss.str();
    return true;
}

static std::string privateKey() {
    const char* e = getenv("QUARK_SIGNING_KEY");
    if (e && *e) return e;
    std::string s;
    std::getline(std::cin, s);
    return s;
}

int main(int argc, char** argv) {
    std::string cmd = argc > 1 ? argv[1] : "";
    if (cmd == "keygen") {
        std::string priv, pub;
        if (!sign::keygen(&priv, &pub)) { fprintf(stderr, "no random source\n"); return 1; }
        printf("private (keep secret, goes in the QUARK_SIGNING_KEY secret): %s\npublic  (goes in src/common/sign.cpp):       %s\n",
               priv.c_str(), pub.c_str());
        return 0;
    }
    if (cmd == "pubkey") {
        std::string pub;
        if (!sign::publicFromPrivate(privateKey(), &pub)) { fprintf(stderr, "bad private key (expected 64 hex digits)\n"); return 1; }
        printf("%s\n", pub.c_str());
        return 0;
    }
    if (cmd == "sign" && argc == 3) {
        std::string key = privateKey(), sig, err;
        if (key.empty()) { fprintf(stderr, "QUARK_SIGNING_KEY is not set\n"); return 1; }
        std::string pub;
        if (sign::publicFromPrivate(key, &pub) && pub != sign::kReleasePublicKey)
            fprintf(stderr, "warning: this key is not the release key built into the launcher (%s)\n", pub.c_str());
        if (!sign::signFile(argv[2], key, &sig, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        std::ofstream(std::string(argv[2]) + ".sig") << sig << "\n";
        printf("signed %s\n", argv[2]);
        return 0;
    }
    if (cmd == "sign-master" && argc == 3) {   // docs/SIGNING.md "Moving the ranking / server-list Worker"
        std::string key = privateKey(), sig, body, pub;
        if (key.empty()) { fprintf(stderr, "QUARK_SIGNING_KEY is not set\n"); return 1; }
        if (sign::publicFromPrivate(key, &pub) && pub != sign::kReleasePublicKey)
            fprintf(stderr, "warning: this key is not the release key built into the game (%s)\n", pub.c_str());
        if (!readAll(argv[2], &body)) { fprintf(stderr, "cannot read %s\n", argv[2]); return 1; }
        if (!sign::signData(sign::kMasterSignPrefix + body, key, &sig)) { fprintf(stderr, "bad private key\n"); return 1; }
        std::ofstream(std::string(argv[2]) + ".sig") << sig << "\n";
        printf("signed %s (as master.txt)\n", argv[2]);
        return 0;
    }
    if (cmd == "verify-master" && (argc == 3 || argc == 4)) {
        std::string body, sig;
        if (!readAll(argv[2], &body) || !readAll(std::string(argv[2]) + ".sig", &sig) ||
            !sign::verifyData(sign::kMasterSignPrefix + body, sig, argc == 4 ? argv[3] : sign::kReleasePublicKey)) {
            fprintf(stderr, "%s: BAD master.txt SIGNATURE\n", argv[2]);
            return 1;
        }
        printf("%s: master.txt signature OK\n", argv[2]);
        return 0;
    }
    if (cmd == "verify" && (argc == 3 || argc == 4)) {
        std::ifstream f(std::string(argv[2]) + ".sig");
        std::stringstream ss;
        ss << f.rdbuf();
        std::string err;
        if (!f || !sign::verifyFile(argv[2], ss.str(), argc == 4 ? argv[3] : sign::kReleasePublicKey, &err)) {
            fprintf(stderr, "%s: BAD SIGNATURE (%s)\n", argv[2], f ? err.c_str() : "no .sig file");
            return 1;
        }
        printf("%s: signature OK\n", argv[2]);
        return 0;
    }
    fprintf(stderr, "usage: quark_sign keygen | pubkey | sign <file> | verify <file> [pubkey] | sign-master <file> | verify-master <file> [pubkey]\n");
    return 2;
}
