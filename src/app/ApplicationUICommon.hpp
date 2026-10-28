#ifndef ZALO10_APPLICATIONUI_COMMON_HPP
#define ZALO10_APPLICATIONUI_COMMON_HPP

#include "applicationui.hpp"
#include "core/ServiceHandoff.hpp"
#include "core/ZaloService.hpp"
#include "utils/ZaloServiceUtils.hpp"
#include "ui/ActiveFrameCover.hpp"

#include <bb/cascades/Application>
#include <bb/cascades/QmlDocument>
#include <bb/cascades/AbstractPane>
#include <bb/cascades/LocaleHandler>
#include <bb/cascades/ThemeSupport>
#include <bb/system/InvokeManager>
#include <bb/system/InvokeRequest>
#include <bb/system/InvokeTargetReply>
#include <bb/system/InvokeQueryTargetsRequest>
#include <bb/system/InvokeQueryTargetsReply>
#include <bb/system/InvokeAction>
#include <bb/system/InvokeTarget>
#include <bb/system/Clipboard>
#include <bb/ApplicationInfo>
#include <bb/pim/calendar/CalendarService>
#include <bb/pim/calendar/CalendarEvent>
#include <bb/pim/calendar/CalendarFolder>
#include <bb/pim/calendar/Result>

#include <QTranslator>
#include <QLocale>
#include <QCoreApplication>
#include <QSettings>
#include <QDebug>
#include <QFile>
#include <QDir>
#include <QDateTime>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QStringList>
#include <QRegExp>
#include <QVariant>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QSslError>
#include <QList>

#include <unistd.h>

using namespace bb::cascades;

using namespace bb::system;

#endif
