#include "ppbemulator.h"
#include <QDateTime>
#include <QThread>
#include <cstring>
#include <QtEndian>
#include "../PPB_Tester_Software/core/communication/ppbprotocol.h"

// ==================== AddressState ====================
PPBEmulator::AddressState::AddressState() :
    isReceivingPRBS(false),
    isSendingPRBS(false),
    isFUTransmit(false),
    isSendingInProgress(false),
    isReceivingFirmware(false),
    expectedDataSize(0),
    fuPeriod(0),
    tuErrorCount(0),
    fuErrorCount(0),
    droppedPacketCount(0),
    checksum(0),
    firmwareSize(0),
    receivedChecksum(0),
    controlFlags(0x80),  // питание включено (бит 7)
    powerOn(true)
{
    memset(fuData, 0, 3);
}

void PPBEmulator::AddressState::reset() {
    isReceivingPRBS = false;
    isSendingPRBS = false;
    isFUTransmit = false;
    isSendingInProgress = false;
    isReceivingFirmware = false;
    fuPeriod = 0;
    tuErrorCount = 0;
    fuErrorCount = 0;
    droppedPacketCount = 0;
    checksum = 0;
    firmwareSize = 0;
    receivedChecksum = 0;
    memset(fuData, 0, 3);
    testData.clear();
    sentData.clear();
    firmwareData.clear();
    receivedFirmware.clear();
    clientAddress.clear();
    clientPort = 0;
    expectedDataSize = 0;
    // controlFlags не сбрасываем, питание остаётся включённым
    // датчики тоже оставляем
}

void PPBEmulator::AddressState::generateFirmwareData(uint32_t size) {
    firmwareData.clear();
    firmwareData.resize(size * 2);
    for (uint32_t i = 0; i < size * 2; ++i) {
        firmwareData[i] = i % 256;
    }
    calculateCRC32();
}

void PPBEmulator::AddressState::calculateCRC32() {
    uint32_t crc = 0xFFFFFFFF;
    for (int i = 0; i < firmwareData.size(); ++i) {
        crc ^= (uint32_t)firmwareData[i] << 24;
        for (int j = 0; j < 8; ++j) {
            if (crc & 0x80000000)
                crc = (crc << 1) ^ 0x04C11DB7;
            else
                crc <<= 1;
        }
    }
    checksum = crc ^ 0xFFFFFFFF;
}

void PPBEmulator::AddressState::updateSensors() {
    auto rnd = QRandomGenerator::global();
    sensors.power1 = qBound(1U, sensors.power1 + rnd->bounded(-1, 2), 10U);
    sensors.power2 = qBound(1U, sensors.power2 + rnd->bounded(-1, 2), 10U);
    sensors.temp1 = qBound(20, sensors.temp1 + rnd->bounded(-2, 3), 80);
    sensors.temp2 = qBound(20, sensors.temp2 + rnd->bounded(-2, 3), 80);
    sensors.temp3 = qBound(20, sensors.temp3 + rnd->bounded(-2, 3), 80);
    sensors.temp4 = qBound(20, sensors.temp4 + rnd->bounded(-2, 3), 80);
    sensors.tempV1 = qBound(20, sensors.tempV1 + rnd->bounded(-2, 3), 80);
    sensors.tempV2 = qBound(20, sensors.tempV2 + rnd->bounded(-2, 3), 80);
    sensors.tempRadIn = qBound(20, sensors.tempRadIn + rnd->bounded(-2, 3), 80);
    sensors.tempRadOut = qBound(20, sensors.tempRadOut + rnd->bounded(-2, 3), 80);
    sensors.vswr1 = qBound(100U, sensors.vswr1 + rnd->bounded(-5, 6), 300U);
    sensors.vswr2 = qBound(100U, sensors.vswr2 + rnd->bounded(-5, 6), 300U);
}

// ==================== PPBEmulator ====================
PPBEmulator::PPBEmulator(QObject* parent)
    : QObject(parent)
    , m_timer(new QTimer(this))
    , m_sensorTimer(new QTimer(this))
    , m_realInterferences(false)
    , m_errorProbability(0.05)
    , m_activeMask(0xFFFF)
{
    // Предварительное создание состояний для всех возможных адресов не требуется,
    // они будут создаваться при первом обращении. Но можно инициализировать 16 состояний,
    // чтобы датчики сразу обновлялись.
    for (int i = 0; i < 16; ++i) {
        m_addressStates[1 << i] = AddressState();
    }

    // Таймер для сброса зависших состояний
    m_timer->setInterval(10000);
    connect(m_timer, &QTimer::timeout, this, [this]() {
        for (auto& state : m_addressStates) {
            if (state.isReceivingPRBS || state.isReceivingFirmware) {
                state.reset();
            }
        }
    });
    m_timer->start();

    // Таймер обновления датчиков (каждые 5 секунд)
    m_sensorTimer->setInterval(5000);
    connect(m_sensorTimer, &QTimer::timeout, this, [this]() {
        for (auto& state : m_addressStates) {
            state.updateSensors();
        }
        log("Датчики обновлены");
    });
    m_sensorTimer->start();

    log("Эмулятор ППБ запущен (поддержка множественных адресов)");
}

PPBEmulator::~PPBEmulator()
{
    m_timer->stop();
    m_sensorTimer->stop();
}

bool PPBEmulator::isSinglePPBAddress(uint16_t addr) const {
    return addr != 0 && (addr & (addr - 1)) == 0 && addr != BROADCAST_ADDRESS;
}

bool PPBEmulator::isAllowedAddress(uint16_t addr) const {
    return (addr == BROADCAST_ADDRESS) || (addr != 0 && (addr & (addr - 1)) == 0);
}

QByteArray PPBEmulator::processPacket(const QByteArray& data,
                                      const QHostAddress& sender,
                                      quint16 senderPort)
{
    log(QString("Получено %1 байт от %2:%3").arg(data.size()).arg(sender.toString()).arg(senderPort), "DEBUG");

    if (data.size() < static_cast<int>(sizeof(BaseRequest))) {
        processData(data, sender, senderPort);
        return QByteArray();
    }

    BaseRequest request;
    memcpy(&request, data.constData(), sizeof(BaseRequest));
    Sign sign = static_cast<Sign>(request.sign);

    if (sign == Sign::IsYou) {
        return processIsYou(request, sender, senderPort);
    }

    if (sign == Sign::TU) {
        uint16_t addr = qFromBigEndian(request.address);
        QByteArray payload = data.mid(sizeof(BaseRequest));

        // Широковещательный адрес – обновляем состояния, но ответ не шлём
        if (addr == BROADCAST_ADDRESS) {
            log("Широковещательная TU-команда, обработка без ответа", "DEBUG");
            // Можно, при желании, обновить состояния всех ППБ, но обычно широковещательные команды не требуют ответа.
            return QByteArray();
        }

        // Одиночный адрес
        if (isSinglePPBAddress(addr)) {
            return processTUCommandForAddress(addr, request, payload, sender, senderPort);
        }

        // Маска нескольких ППБ
        uint16_t mask = addr & m_activeMask;
        for (int i = 0; i < 16; ++i) {
            if (mask & (1 << i)) {
                uint16_t singleAddr = 1 << i;
                QByteArray response = processTUCommandForAddress(singleAddr, request, payload, sender, senderPort);
                if (!response.isEmpty()) {
                    emit sendPacketRequest(response, sender, senderPort);
                }
            }
        }
        return QByteArray(); // ответы уже отправлены через сигнал
    }

    if (sign == Sign::FU) {
        return processFUBridge(request, sender, senderPort);
    }

    // Неизвестный признак
    processData(data, sender, senderPort);
    return QByteArray();
}

QByteArray PPBEmulator::processIsYou(const BaseRequest& request,
                                     const QHostAddress& sender,
                                     quint16 senderPort)
{
    Q_UNUSED(request);
    uint16_t maskBE = qToBigEndian(m_activeMask);
    QByteArray response(reinterpret_cast<const char*>(&maskBE), sizeof(maskBE));
    log(QString("IsYou запрос от %1:%2, отвечаем маской 0x%3")
            .arg(sender.toString()).arg(senderPort).arg(m_activeMask, 4, 16, QChar('0')));
    return response;
}

QByteArray PPBEmulator::processTUCommandForAddress(uint16_t address,
                                                   const BaseRequest& request,
                                                   const QByteArray& payload,
                                                   const QHostAddress& sender,
                                                   quint16 senderPort)
{
    // Получаем или создаём состояние для данного адреса
    AddressState& state = m_addressStates[address];
    state.clientAddress = sender;
    state.clientPort = senderPort;

    TechCommand techCmd = static_cast<TechCommand>(request.command);
    log(QString("Обработка TU-команды 0x%1 для адреса 0x%2")
            .arg(static_cast<int>(techCmd), 2, 16, QChar('0'))
            .arg(address, 4, 16, QChar('0')));

    switch (techCmd) {
    case TechCommand::TS:
        return handleTSCommand(state, address);
    case TechCommand::TC:
        return handleTCCommand(state, address, payload);
    case TechCommand::VERS:
        return handleVERSCommand(state, address);
    case TechCommand::VOLUME:
        return handleVOLUMECommand(state, address);
    case TechCommand::CHECKSUM:
        return handleCHECKSUMCommand(state, address);
    case TechCommand::PROGRAMM:
        return handlePROGRAMMCommand(state, address);
    case TechCommand::CLEAN:
        return handleCLEANCommand(state, address);
    case TechCommand::PRBS_M2S:
        return handlePRBS_M2SCommand(state, address, payload);
    case TechCommand::BER_T:
        return handleBERTCommand(state, address);
    case TechCommand::BER_F:
        return handleBERFCommand(state, address);
    case TechCommand::PRBS_S2M:
        return handlePRBS_S2MCommand(state, address, sender, senderPort);
    case TechCommand::DROP:
        return handleDROPCommand(state, address);
    default:
        log(QString("Неизвестная команда 0x%1 для адреса 0x%2")
                .arg(static_cast<int>(techCmd), 2, 16, QChar('0'))
                .arg(address, 4, 16, QChar('0')), "WARNING");
        return createTUResponseHeader(address, 0x01); // ошибка
    }
}

QByteArray PPBEmulator::processFUBridge(const BaseRequest& request,
                                        const QHostAddress& sender,
                                        quint16 senderPort)
{
    // Пока поддерживаем только адрес 0x0001 (ППБ1)
    AddressState& state = m_addressStates[0x0001];
    log(QString("Переходник: ФУ команда, адрес=0x%1, команда=0x%2, период=%3")
            .arg(request.address, 4, 16, QChar('0'))
            .arg(request.command, 2, 16, QChar('0'))
            .arg(request.fu_period));

    state.clientAddress = sender;
    state.clientPort = senderPort;

    BridgeResponse response;
    response.address = 0x0001;
    response.command = request.command;
    response.status = 1; // OK

    if (request.command == 0x00) { // ФУ передача
        state.isFUTransmit = true;
        state.fuPeriod = request.fu_period;
        log("ППБ1: ФУ передача включена");
    } else if (request.command == 0x01) { // ФУ приём
        state.isFUTransmit = false;
        memcpy(state.fuData, request.fu_data, 3);
        log(QString("ППБ1: ФУ приём, данные [0x%1,0x%2,0x%3]")
                .arg(request.fu_data[0],2,16)
                .arg(request.fu_data[1],2,16)
                .arg(request.fu_data[2],2,16));
    }

    return QByteArray((const char*)&response, sizeof(response));
}

void PPBEmulator::processData(const QByteArray& data,
                              const QHostAddress& sender,
                              quint16 senderPort)
{
    // Пока поддерживаем только приём прошивки для адреса 0x0001
    AddressState& state = m_addressStates[0x0001];

    if (state.clientAddress != sender || state.clientPort != senderPort) {
        log(QString("ППБ1: данные от неизвестного клиента %1:%2").arg(sender.toString()).arg(senderPort), "DEBUG");
        return;
    }

    if (state.isReceivingFirmware) {
        if (data.size() == state.firmwareSize * 2) {
            QByteArray received = data;
            if (m_realInterferences) {
                applyInterference(received);
            }
            state.receivedFirmware = received;
            state.receivedChecksum = computeCRC32(received);
            state.isReceivingFirmware = false;
            log(QString("ППБ1: приём файла ПО завершён (%1 байт), CRC=%2")
                    .arg(data.size()).arg(state.receivedChecksum, 8, 16, QChar('0')));
        } else {
            log(QString("ППБ1: неверный размер данных ПО: %1 (ожидалось %2)")
                    .arg(data.size()).arg(state.firmwareSize * 2), "WARNING");
            state.isReceivingFirmware = false;
        }
    }
}

// ==================== Обработчики команд ТУ ====================

QByteArray PPBEmulator::handleTSCommand(AddressState& state, uint16_t address)
{
    log(QString("ППБ 0x%1: команда TS").arg(address, 4, 16, QChar('0')));
    QByteArray response = createTUResponseHeader(address, 0x00);

    // Маска валидации: все данные достоверны (питание всегда включено)
    uint32_t validMask = 0x0001FFFF;
    uint32_t maskBE = qToBigEndian(validMask);
    response.append(reinterpret_cast<const char*>(&maskBE), 4);

    // state_mask (2 байта) – используем controlFlags, сохраняя только биты 0,1,2,7
    uint16_t stateMask = state.controlFlags & 0x87;
    uint16_t stateMaskBE = qToBigEndian(stateMask);
    response.append(reinterpret_cast<const char*>(&stateMaskBE), 2);

    // power1 (4 байта)
    uint32_t power1BE = qToBigEndian(state.sensors.power1);
    response.append(reinterpret_cast<const char*>(&power1BE), 4);

    // power2
    uint32_t power2BE = qToBigEndian(state.sensors.power2);
    response.append(reinterpret_cast<const char*>(&power2BE), 4);

    // температуры (каждая по 2 байта)
    uint16_t t1BE = qToBigEndian(state.sensors.temp1);
    response.append(reinterpret_cast<const char*>(&t1BE), 2);
    uint16_t t2BE = qToBigEndian(state.sensors.temp2);
    response.append(reinterpret_cast<const char*>(&t2BE), 2);
    uint16_t t3BE = qToBigEndian(state.sensors.temp3);
    response.append(reinterpret_cast<const char*>(&t3BE), 2);
    uint16_t t4BE = qToBigEndian(state.sensors.temp4);
    response.append(reinterpret_cast<const char*>(&t4BE), 2);
    uint16_t tv1BE = qToBigEndian(state.sensors.tempV1);
    response.append(reinterpret_cast<const char*>(&tv1BE), 2);
    uint16_t tv2BE = qToBigEndian(state.sensors.tempV2);
    response.append(reinterpret_cast<const char*>(&tv2BE), 2);
    uint16_t tinBE = qToBigEndian(state.sensors.tempRadIn);
    response.append(reinterpret_cast<const char*>(&tinBE), 2);
    uint16_t toutBE = qToBigEndian(state.sensors.tempRadOut);
    response.append(reinterpret_cast<const char*>(&toutBE), 2);

    // КСВН (4 байта каждое)
    uint32_t vswr1BE = qToBigEndian(state.sensors.vswr1);
    response.append(reinterpret_cast<const char*>(&vswr1BE), 4);
    uint32_t vswr2BE = qToBigEndian(state.sensors.vswr2);
    response.append(reinterpret_cast<const char*>(&vswr2BE), 4);

    return response;
}

QByteArray PPBEmulator::handleTCCommand(AddressState& state, uint16_t address, const QByteArray& payload)
{
    log(QString("ППБ 0x%1: команда TC").arg(address, 4, 16, QChar('0')));
    if (payload.size() < static_cast<int>(sizeof(TCDataPayload))) {
        log("Недостаточно данных для TC", "WARNING");
        return createTUResponseHeader(address, 0x01);
    }

    TCDataPayload tcData;
    memcpy(&tcData, payload.constData(), sizeof(TCDataPayload));
    tcData.power1 = qFromBigEndian(tcData.power1);
    tcData.power2 = qFromBigEndian(tcData.power2);

    // Обновляем мощности
    state.sensors.power1 = tcData.power1;
    state.sensors.power2 = tcData.power2;

    // Сохраняем флаги состояния (биты 0,1,2,7), бит питания (7) игнорируем, т.к. питание всегда включено
    state.controlFlags = (state.controlFlags & 0x80) | (tcData.stateMask & 0x07);

    // Сброс ошибок (бит 2)
    if (tcData.stateMask & 0x04) {
        state.tuErrorCount = 0;
        state.fuErrorCount = 0;
        state.droppedPacketCount = 0;
        log("Сброс ошибок");
    }

    // Перезагрузка (бит 1) – сбрасываем временные данные, но сохраняем мощности и controlFlags
    if (tcData.stateMask & 0x02) {
        state.reset(); // reset не трогает controlFlags и датчики
        log("Перезагрузка");
    }

    // Блокировка (бит 0) просто сохраняется в controlFlags, дополнительных действий не требует

    return createTUResponseHeader(address, 0x00);
}

QByteArray PPBEmulator::handleVERSCommand(AddressState& state, uint16_t address)
{
    log(QString("ППБ 0x%1: команда VERS").arg(address, 4, 16, QChar('0')));
    if (state.firmwareData.isEmpty()) {
        state.generateFirmwareData(1024);
    }
    QByteArray response = createTUResponseHeader(address, 0x00);
    response.append(createCRC32Data(state.checksum));
    response.append(QByteArray(4, 0)); // дополнение до 8 байт
    return response;
}

QByteArray PPBEmulator::handleVOLUMECommand(AddressState& state, uint16_t address)
{
    log(QString("ППБ 0x%1: команда VOLUME").arg(address, 4, 16, QChar('0')));
    state.isReceivingFirmware = true;
    state.receivedFirmware.clear();
    state.firmwareSize = 1024;
    state.expectedDataSize = state.firmwareSize * 2;
    return createTUResponseHeader(address, 0x00);
}

QByteArray PPBEmulator::handleCHECKSUMCommand(AddressState& state, uint16_t address)
{
    log(QString("ППБ 0x%1: команда CHECKSUM").arg(address, 4, 16, QChar('0')));
    QByteArray response = createTUResponseHeader(address, 0x00);
    response.append(createCRC32Data(state.receivedChecksum));
    response.append(QByteArray(4, 0));
    return response;
}

QByteArray PPBEmulator::handlePROGRAMMCommand(AddressState& state, uint16_t address)
{
    log(QString("ППБ 0x%1: команда PROGRAMM").arg(address, 4, 16, QChar('0')));
    state.receivedFirmware.clear();
    return createTUResponseHeader(address, 0x00);
}

QByteArray PPBEmulator::handleCLEANCommand(AddressState& state, uint16_t address)
{
    log(QString("ППБ 0x%1: команда CLEAN").arg(address, 4, 16, QChar('0')));
    state.receivedFirmware.clear();
    return createTUResponseHeader(address, 0x00);
}

QByteArray PPBEmulator::handlePRBS_M2SCommand(AddressState& state, uint16_t address, const QByteArray& payload)
{
    log(QString("ППБ 0x%1: команда PRBS_M2S, получено %2 байт").arg(address, 4, 16, QChar('0')).arg(payload.size()));

    if (payload.size() != 1024) {
        log("Неверный размер данных PRBS", "WARNING");
        return createTUResponseHeader(address, 0x01);
    }

    if (m_realInterferences) {
        QByteArray corrupted = payload;
        applyInterference(corrupted);
        state.testData = corrupted;
    } else {
        state.testData = payload;
    }

    analyzePRBS(state);
    log(QString("PRBS_M2S завершён, ошибок: %1").arg(state.tuErrorCount));
    return createTUResponseHeader(address, 0x00);
}

QByteArray PPBEmulator::handleBERTCommand(AddressState& state, uint16_t address)
{
    log(QString("ППБ 0x%1: команда BER_T").arg(address, 4, 16, QChar('0')));
    QByteArray response = createTUResponseHeader(address, 0x00);
    response.append(createErrorCountData(state.tuErrorCount));
    return response;
}

QByteArray PPBEmulator::handleBERFCommand(AddressState& state, uint16_t address)
{
    log(QString("ППБ 0x%1: команда BER_F").arg(address, 4, 16, QChar('0')));
    QByteArray response = createTUResponseHeader(address, 0x00);
    response.append(createErrorCountData(state.fuErrorCount));
    return response;
}

QByteArray PPBEmulator::handlePRBS_S2MCommand(AddressState& state, uint16_t address,
                                              const QHostAddress& sender, quint16 senderPort)
{
    log(QString("ППБ 0x%1: команда PRBS_S2M").arg(address, 4, 16, QChar('0')));
    if (state.testData.isEmpty()) {
        log("PRBS_S2M: данные отсутствуют, генерируем заглушку", "WARNING");
        state.testData = generatePRBSSequence();
    }
    QByteArray response = createTUResponseHeader(address, 0x00);
    response.append(state.testData);
    return response;
}

QByteArray PPBEmulator::handleDROPCommand(AddressState& state, uint16_t address)
{
    log(QString("ППБ 0x%1: команда DROP").arg(address, 4, 16, QChar('0')));
    QByteArray response = createTUResponseHeader(address, 0x00);
    response.append(createErrorCountData(state.droppedPacketCount));
    return response;
}

// ==================== Формирование ответов ====================

QByteArray PPBEmulator::createTUResponseHeader(uint16_t address, uint8_t status)
{
    TUResponseHeader header;
    header.address = qToBigEndian(address);
    header.status = status;
    return QByteArray((const char*)&header, sizeof(header));
}

QByteArray PPBEmulator::createCRC32Data(uint32_t crc)
{
    uint32_t crcBE = qToBigEndian(crc);
    return QByteArray((const char*)&crcBE, sizeof(crcBE));
}

QByteArray PPBEmulator::createErrorCountData(uint32_t count)
{
    uint32_t countBE = qToBigEndian(count);
    return QByteArray((const char*)&countBE, sizeof(countBE));
}

// ==================== PRBS ====================

QByteArray PPBEmulator::generatePRBSSequence()
{
    QByteArray data;
    data.resize(1024);
    for (int i = 0; i < 512; ++i) {
        uint16_t value = i * 257;
        data[i*2] = value & 0xFF;
        data[i*2+1] = (value >> 8) & 0xFF;
    }
    return data;
}

void PPBEmulator::analyzePRBS(AddressState& state)
{
    QByteArray expected = generatePRBSSequence();
    if (state.testData.size() != expected.size()) {
        state.tuErrorCount = 0;
        log("Анализ PRBS: неверный размер данных", "WARNING");
        return;
    }

    int errors = 0;
    for (int i = 0; i < expected.size(); ++i) {
        if (state.testData[i] != expected[i])
            errors++;
    }
    state.tuErrorCount = errors;
}

// ==================== Помехи ====================

bool PPBEmulator::applyInterference(QByteArray& data)
{
    if (!m_realInterferences) return false;
    if (QRandomGenerator::global()->generateDouble() >= m_errorProbability) return false;

    int pos = QRandomGenerator::global()->bounded(data.size());
    data[pos] ^= 0x01;
    log(QString("Помеха применена к байту %1").arg(pos), "DEBUG");
    return true;
}

// ==================== CRC32 ====================

uint32_t PPBEmulator::computeCRC32(const QByteArray& data)
{
    uint32_t crc = 0xFFFFFFFF;
    for (int i = 0; i < data.size(); ++i) {
        crc ^= (uint32_t)(uint8_t)data[i] << 24;
        for (int j = 0; j < 8; ++j) {
            if (crc & 0x80000000)
                crc = (crc << 1) ^ 0x04C11DB7;
            else
                crc <<= 1;
        }
    }
    return crc ^ 0xFFFFFFFF;
}

// ==================== Логирование ====================

void PPBEmulator::log(const QString& message, const QString& level)
{
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
    qDebug().noquote() << QString("[%1] [%2] %3").arg(timestamp, level, message);
}
