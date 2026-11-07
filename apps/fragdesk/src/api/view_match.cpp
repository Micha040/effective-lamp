// FragDesk – Handy-App: ein Match mit allen Bereichen (wie page_match.cpp)
#include "views.h"

namespace fdapi {

void viewMatch(ViewCtx& c, Response& res, const std::string& checksum) {
    (void)c;
    (void)checksum;
    res.error(501, "Kommt gleich");
}

}  // namespace fdapi
