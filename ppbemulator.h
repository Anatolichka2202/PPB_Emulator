#ifndef PPBEMULATOR_H
#define PPBEMULATOR_H

#include <QObject>
#include <QTimer>
#include <QMap>
#include <QHostAddress>
#include <QRandomGenerator>
#include "../PPB_Tester_Software/core/communication/ppbprotocol.h"

class PPBEmulator : public QObject {
    Q_OBJECT

signals:
    void sendPacketRequest(const QByteArray& packet, const QHostAddress& address, quint16 port);

public:
    explicit PPBEmulator(QObject* parent = nullptr);
    ~PPBEmulator();

    QByteArray processPacket(const QByteArray& data,
                             const QHostAddress& sender,
                             quint16 senderPort);

    void setRealInterferences(bool enabled, double probability = 0.05) {
        m_realInterferences = enabled;
        m_errorProbability = probability;
        log(QString("Режим помех: %1, вероятность: %2%")
                .arg(enabled ? "включен" : "выключен")
                .arg(probability * 100, 0, 'f', 2));
    }

    bool realInterferences() const { return m_realInterferences; }

private:
    struct SensorData {
        uint32_t power1 = 4;
        uint32_t power2 = 5;
        uint16_t temp1 = 45, temp2 = 46, temp3 = 47, temp4 = 48;
        uint16_t tempV1 = 49, tempV2 = 50;
        uint16_t tempRadIn = 51, tempRadOut = 52;
        uint32_t vswr1 = 120, vswr2 = 130;
    };

    struct AddressState {
        QByteArray testData;                    // Принятая тестовая последовательность (PRBS_M2S)
        QByteArray sentData;                     // Сгенерированная тестовая последовательность (PRBS_S2M)
        QByteArray firmwareData;                  // Данные файла ПО для VOLUME
        QByteArray receivedFirmware;              // Принятый файл ПО

        bool isReceivingPRBS;
        bool isSendingPRBS;
        bool isFUTransmit;
        bool isSendingInProgress;
        bool isReceivingFirmware;
        int expectedDataSize;

        uint8_t fuPeriod;
        uint8_t fuData[3];

        uint32_t tuErrorCount;
        uint32_t fuErrorCount;
        uint32_t droppedPacketCount;

        uint32_t checksum;
        uint32_t firmwareSize;
        uint32_t receivedChecksum;

        SensorData sensors;
        uint8_t controlFlags;   // биты: 0 – блокировка, 1 – перезагрузка, 2 – сброс ошибок, 7 – питание
        bool powerOn;           // всегда true (питание включено)

        QHostAddress clientAddress;
        quint16 clientPort;

        AddressState();
        void reset();
        void generateFirmwareData(uint32_t size);
        void calculateCRC32();
        void updateSensors();
    };

    // Проверка, является ли адрес одиночным (степень двойки)
    bool isSinglePPBAddress(uint16_t addr) const;

    // Проверка, допустим ли адрес (одиночный или широковещательный)
    bool isAllowedAddress(uint16_t addr) const;

    // Обработка TU-команды для конкретного адреса
    QByteArray processTUCommandForAddress(uint16_t address,
                                          const BaseRequest& request,
                                          const QByteArray& payload,
                                          const QHostAddress& sender,
                                          quint16 senderPort);

    // Обработка FU-команд (пока только для адреса 0x0001)
    QByteArray processFUBridge(const BaseRequest& request,
                               const QHostAddress& sender,
                               quint16 senderPort);

    // Обработка данных (приём прошивки и т.п.) – пока для адреса 0x0001
    void processData(const QByteArray& data,
                     const QHostAddress& sender,
                     quint16 senderPort);

    // Обработчики команд ТУ
    QByteArray handleTSCommand(AddressState& state, uint16_t address);
    QByteArray handleTCCommand(AddressState& state, uint16_t address, const QByteArray& payload);
    QByteArray handleVERSCommand(AddressState& state, uint16_t address);
    QByteArray handleVOLUMECommand(AddressState& state, uint16_t address);
    QByteArray handleCHECKSUMCommand(AddressState& state, uint16_t address);
    QByteArray handlePROGRAMMCommand(AddressState& state, uint16_t address);
    QByteArray handleCLEANCommand(AddressState& state, uint16_t address);
    QByteArray handlePRBS_M2SCommand(AddressState& state, uint16_t address, const QByteArray& payload);
    QByteArray handleBERTCommand(AddressState& state, uint16_t address);
    QByteArray handleBERFCommand(AddressState& state, uint16_t address);
    QByteArray handlePRBS_S2MCommand(AddressState& state, uint16_t address,
                                     const QHostAddress& sender, quint16 senderPort);
    QByteArray handleDROPCommand(AddressState& state, uint16_t address);

    // Обработка команды IsYou (признак 3)
    QByteArray processIsYou(const BaseRequest& request,
                            const QHostAddress& sender,
                            quint16 senderPort);

    // Формирование общих частей ответов
    QByteArray createTUResponseHeader(uint16_t address, uint8_t status);
    QByteArray createCRC32Data(uint32_t crc);
    QByteArray createErrorCountData(uint32_t count);

    // Генерация и анализ PRBS (1024 байта)
    QByteArray generatePRBSSequence();
    void analyzePRBS(AddressState& state);

    // Применение помех
    bool applyInterference(QByteArray& data);

    // Логирование
    void log(const QString& message, const QString& level = "INFO");

    // CRC32
    uint32_t computeCRC32(const QByteArray& data);

private:
    QMap<uint16_t, AddressState> m_addressStates;  // состояние для каждого адреса
    QTimer* m_timer;               // таймер сброса зависших состояний
    QTimer* m_sensorTimer;         // таймер обновления датчиков
    bool m_realInterferences;
    double m_errorProbability;
    uint16_t m_activeMask;          // маска активных ППБ (по умолчанию 0xFFFF)
};

#endif // PPBEMULATOR_H
