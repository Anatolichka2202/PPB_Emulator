#include "ppbemulator.h"
#include "udpserver.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QDateTime>

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    QCoreApplication::setApplicationName("PPB Emulator");
    QCoreApplication::setApplicationVersion("1.0");

    QCommandLineParser parser;
    parser.setApplicationDescription("Эмулятор ППБ для тестирования опросника");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption interferencesOption("i", "Включить эмуляцию помех");
    parser.addOption(interferencesOption);

    QCommandLineOption errorProbabilityOption("p", "Вероятность ошибки (0.0-1.0)", "probability", "0.05");
    parser.addOption(errorProbabilityOption);

    // Опционально: маска активных ППБ
    QCommandLineOption maskOption("m", "Маска активных ППБ (hex)", "mask", "FFFF");
    parser.addOption(maskOption);

    parser.process(app);

    PPBEmulator emulator;

    // Установка маски активных ППБ (если передана)
    if (parser.isSet(maskOption)) {
        bool ok;
        uint16_t mask = parser.value(maskOption).toUShort(&ok, 16);
        if (ok) {
            // Здесь нужен метод setActiveMask, но пока не реализован
            // Можно добавить позже
        }
    }

    if (parser.isSet(interferencesOption)) {
        double probability = parser.value(errorProbabilityOption).toDouble();
        emulator.setRealInterferences(true, probability);
    }

    quint16 port = 8888;
    UDPServer server(&emulator, port, "198.168.0.230");

    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
    qDebug().noquote() << QString("[%1] [INFO] =========================================").arg(timestamp);
    qDebug().noquote() << QString("[%1] [INFO] Эмулятор ППБ запущен (поддержка нескольких ППБ)").arg(timestamp);
    qDebug().noquote() << QString("[%1] [INFO] Адрес: 198.168.0.230:%2").arg(timestamp).arg(port);
    qDebug().noquote() << QString("[%1] [INFO] Маска активных ППБ: 0x%2").arg(timestamp).arg(0xFFFF, 4, 16, QChar('0'));

    if (parser.isSet(interferencesOption)) {
        qDebug().noquote() << QString("[%1] [INFO] Эмуляция помех: ВКЛЮЧЕНА").arg(timestamp);
        qDebug().noquote() << QString("[%1] [INFO] Вероятность ошибки: %2%").arg(timestamp).arg(parser.value(errorProbabilityOption).toDouble() * 100);
    } else {
        qDebug().noquote() << QString("[%1] [INFO] Эмуляция помех: выключена").arg(timestamp);
    }

    qDebug().noquote() << QString("[%1] [INFO] =========================================").arg(timestamp);
    qDebug().noquote() << QString("[%1] [INFO] Ожидание команд...").arg(timestamp);

    return app.exec();
}
