#pragma once

#include <QString>

class QApplication;

namespace devhub::app
{
// Exercises the deployed runtime without bootstrapping or changing user data.
int checkRuntime(QApplication &application, const QString &reportPath = {},
                 const QString &qmlRoot = {});
} // namespace devhub::app
