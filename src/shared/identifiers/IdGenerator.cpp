#include "IdGenerator.h"

namespace devhub::shared::identifiers {

QUuid UuidIdGenerator::create()
{
    return QUuid::createUuid();
}

} // namespace devhub::shared::identifiers
