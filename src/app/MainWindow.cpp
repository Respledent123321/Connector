#include "MainWindow.h"

#include "core/AppPaths.h"
#include "core/AudioUtils.h"

#include <QAbstractItemView>
#include <QAudioFormat>
#include <QAudioSource>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QRegularExpression>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTabWidget>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

#include <algorithm>
#include <cmath>

namespace {
bool trySolveBasicArithmetic(const QString &text, QString *out) {
    if (!out) {
        return false;
    }

    const QRegularExpression directPattern(
        "^\\s*(-?\\d+(?:\\.\\d+)?)\\s*([+\\-*/xX])\\s*(-?\\d+(?:\\.\\d+)?)\\s*$");
    const QRegularExpression phrasedPattern(
        "^\\s*(?:what\\s+is|calculate|compute)\\s+(-?\\d+(?:\\.\\d+)?)\\s*([+\\-*/xX])\\s*(-?\\d+(?:\\.\\d+)?)\\s*\\??\\s*$",
        QRegularExpression::CaseInsensitiveOption);

    QRegularExpressionMatch match = directPattern.match(text);
    if (!match.hasMatch()) {
        match = phrasedPattern.match(text);
    }
    if (!match.hasMatch()) {
        return false;
    }

    bool okA = false;
    bool okB = false;
    const double a = match.captured(1).toDouble(&okA);
    const double b = match.captured(3).toDouble(&okB);
    if (!okA || !okB) {
        return false;
    }

    const QString op = match.captured(2);
    double value = 0.0;
    if (op == "+") {
        value = a + b;
    } else if (op == "-") {
        value = a - b;
    } else if (op == "*" || op == "x" || op == "X") {
        value = a * b;
    } else if (op == "/") {
        if (std::abs(b) < 1e-12) {
            *out = "Cannot divide by zero.";
            return true;
        }
        value = a / b;
    } else {
        return false;
    }

    QString valueText = QString::number(value, 'f', 8);
    while (valueText.contains('.') && (valueText.endsWith('0') || valueText.endsWith('.'))) {
        valueText.chop(1);
    }
    *out = QString("Result: %1 %2 %3 = %4")
               .arg(match.captured(1), op, match.captured(3), valueText);
    return true;
}

bool shouldAutoResearch(const QString &text) {
    const QString t = text.trimmed().toLower();
    if (t.isEmpty()) {
        return false;
    }

    return t.contains("latest") ||
           t.contains("today") ||
           t.contains("current") ||
           t.contains("news") ||
           t.contains("weather") ||
           t.contains("stock") ||
           t.contains("price of") ||
           t.contains("who is") ||
           t.contains("what happened") ||
           t.contains("when is");
}
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      m_toolManager(AppPaths::toolsDir()),
      m_tts(this),
      m_ggwave(this) {
    QDir root(AppPaths::rootDir());
    root.mkpath("data");
    root.mkpath("data/tools");
    root.mkpath("data/logs");
    root.mkpath("data/cache/web");

    m_configManager.load(AppPaths::configPath());

    const QString dbPath = QDir(AppPaths::dataDir()).filePath("state.db");
    const bool dbOk = m_memoryStore.initialize(dbPath);

    m_agent = new AgentController(&m_memoryStore, &m_reasoner, &m_research, &m_toolManager, this);
    m_reasonerWatcher = new QFutureWatcher<QString>(this);
    m_sttWatcher = new QFutureWatcher<QString>(this);
    m_researchWatcher = new QFutureWatcher<ResearchResult>(this);
    m_modelInstallWatcher = new QFutureWatcher<QString>(this);

    setupUi();
    applyConfigToEngines();

    connect(&m_ggwave, &GgWaveService::messageReceived, this, &MainWindow::onGgMessageReceived);
    connect(&m_ggwave, &GgWaveService::statusChanged, this, [this](const QString &msg) {
        if (m_ggLog) {
            m_ggLog->append("[status] " + msg);
        }
    });
    connect(m_reasonerWatcher, &QFutureWatcher<QString>::finished, this, &MainWindow::onReasoningFinished);
    connect(m_sttWatcher, &QFutureWatcher<QString>::finished, this, &MainWindow::onSttFinished);
    connect(m_researchWatcher, &QFutureWatcher<ResearchResult>::finished, this, &MainWindow::onResearchFinished);
    connect(m_modelInstallWatcher, &QFutureWatcher<QString>::finished, this, &MainWindow::onModelInstallFinished);

    if (!dbOk) {
        appendChatLine("system", "Warning: failed to initialize SQLite memory store.");
    } else {
        appendChatLine("system", "Memory store ready at: " + dbPath);
    }

    onRefreshMemories();
    onRefreshTools();
}

MainWindow::~MainWindow() {
    stopPushToTalkCapture();
    m_ggwave.stopListening();
}

void MainWindow::setupUi() {
    setWindowTitle("Robot GUI (Local-Only)");
    resize(1200, 800);

    m_tabs = new QTabWidget(this);
    m_tabs->addTab(buildChatTab(), "Chat");
    m_tabs->addTab(buildSoundTab(), "Sound Link");
    m_tabs->addTab(buildMemoryTab(), "Memory");
    m_tabs->addTab(buildToolsTab(), "Tools");
    m_tabs->addTab(buildResearchTab(), "Research");
    m_tabs->addTab(buildSettingsTab(), "Settings");

    setCentralWidget(m_tabs);
}

QWidget *MainWindow::buildChatTab() {
    QWidget *tab = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(tab);

    m_chatHistory = new QTextEdit(tab);
    m_chatHistory->setReadOnly(true);
    layout->addWidget(m_chatHistory, 1);

    QHBoxLayout *controls = new QHBoxLayout();

    m_chatInput = new QLineEdit(tab);
    m_chatInput->setPlaceholderText("Type a message or command (e.g. /remember, /research, /tool run)...");

    m_sendBtn = new QPushButton("Send", tab);
    m_pttBtn = new QPushButton("Push-to-Talk", tab);
    m_speakCheck = new QCheckBox("Speak responses", tab);
    m_speakCheck->setChecked(m_configManager.config().speakResponses);

    controls->addWidget(m_chatInput, 1);
    controls->addWidget(m_sendBtn);
    controls->addWidget(m_pttBtn);
    controls->addWidget(m_speakCheck);

    layout->addLayout(controls);

    connect(m_sendBtn, &QPushButton::clicked, this, &MainWindow::onSendText);
    connect(m_chatInput, &QLineEdit::returnPressed, this, &MainWindow::onSendText);
    connect(m_pttBtn, &QPushButton::clicked, this, &MainWindow::onTogglePushToTalk);

    return tab;
}

QWidget *MainWindow::buildSoundTab() {
    QWidget *tab = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(tab);

    QHBoxLayout *sendRow = new QHBoxLayout();
    m_ggSendInput = new QLineEdit(tab);
    m_ggSendInput->setPlaceholderText("Text to transmit through ggwave audio...");
    m_ggSendBtn = new QPushButton("Transmit", tab);

    sendRow->addWidget(m_ggSendInput, 1);
    sendRow->addWidget(m_ggSendBtn);

    QHBoxLayout *optionsRow = new QHBoxLayout();
    m_ggProtocolCombo = new QComboBox(tab);
    const QStringList keys = GgWaveService::supportedProtocolKeys();
    for (const QString &k : keys) {
        m_ggProtocolCombo->addItem(GgWaveService::protocolLabel(k), k);
    }

    const int protocolIndex = m_ggProtocolCombo->findData(m_configManager.config().ggwaveProtocol);
    if (protocolIndex >= 0) {
        m_ggProtocolCombo->setCurrentIndex(protocolIndex);
    }

    m_ggVolumeSlider = new QSlider(Qt::Horizontal, tab);
    m_ggVolumeSlider->setRange(1, 100);
    m_ggVolumeSlider->setValue(m_configManager.config().ggwaveTxVolume);
    m_ggVolumeSlider->setToolTip("Transmission volume");
    m_ggRepeatSpin = new QSpinBox(tab);
    m_ggRepeatSpin->setRange(1, 10);
    m_ggRepeatSpin->setValue(m_configManager.config().ggwaveRepeatCount);
    m_ggRepeatSpin->setToolTip("Repeat each chunk to improve reliability");
    m_ggRouteToChatCheck = new QCheckBox("Route received payload to chat", tab);
    m_ggRouteToChatCheck->setChecked(m_configManager.config().ggwaveRouteToChat);

    optionsRow->addWidget(new QLabel("Protocol:", tab));
    optionsRow->addWidget(m_ggProtocolCombo, 1);
    optionsRow->addWidget(new QLabel("Volume:", tab));
    optionsRow->addWidget(m_ggVolumeSlider, 1);
    optionsRow->addWidget(new QLabel("Repeats:", tab));
    optionsRow->addWidget(m_ggRepeatSpin);
    optionsRow->addWidget(m_ggRouteToChatCheck);

    QHBoxLayout *listenRow = new QHBoxLayout();
    m_ggListenBtn = new QPushButton("Start Listening", tab);
    listenRow->addWidget(m_ggListenBtn);
    listenRow->addStretch(1);

    m_ggLog = new QTextEdit(tab);
    m_ggLog->setReadOnly(true);

    layout->addLayout(sendRow);
    layout->addLayout(optionsRow);
    layout->addLayout(listenRow);
    layout->addWidget(m_ggLog, 1);

    connect(m_ggSendBtn, &QPushButton::clicked, this, &MainWindow::onGgSend);
    connect(m_ggListenBtn, &QPushButton::clicked, this, &MainWindow::onToggleGgListen);

    return tab;
}

QWidget *MainWindow::buildMemoryTab() {
    QWidget *tab = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(tab);

    m_memoryTable = new QTableWidget(tab);
    m_memoryTable->setColumnCount(4);
    m_memoryTable->setHorizontalHeaderLabels({"ID", "Timestamp", "Category", "Content"});
    m_memoryTable->horizontalHeader()->setStretchLastSection(true);
    m_memoryTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_memoryTable->setSelectionMode(QAbstractItemView::SingleSelection);

    layout->addWidget(m_memoryTable, 1);

    QHBoxLayout *buttons = new QHBoxLayout();
    QPushButton *refresh = new QPushButton("Refresh", tab);
    QPushButton *forget = new QPushButton("Forget Selected", tab);
    buttons->addWidget(refresh);
    buttons->addWidget(forget);
    buttons->addStretch(1);
    layout->addLayout(buttons);

    connect(refresh, &QPushButton::clicked, this, &MainWindow::onRefreshMemories);
    connect(forget, &QPushButton::clicked, this, &MainWindow::onForgetSelectedMemory);

    return tab;
}

QWidget *MainWindow::buildToolsTab() {
    QWidget *tab = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(tab);

    m_toolsTable = new QTableWidget(tab);
    m_toolsTable->setColumnCount(4);
    m_toolsTable->setHorizontalHeaderLabels({"Name", "Command", "Arguments", "Enabled"});
    m_toolsTable->horizontalHeader()->setStretchLastSection(true);
    m_toolsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_toolsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_toolsTable, 1);

    QFormLayout *createForm = new QFormLayout();
    m_toolNameInput = new QLineEdit(tab);
    m_toolCmdInput = new QLineEdit(tab);
    m_toolArgsInput = new QLineEdit(tab);
    m_toolDescInput = new QLineEdit(tab);

    m_toolArgsInput->setPlaceholderText("arg1 arg2 ...");

    createForm->addRow("Name", m_toolNameInput);
    createForm->addRow("Command", m_toolCmdInput);
    createForm->addRow("Arguments", m_toolArgsInput);
    createForm->addRow("Description", m_toolDescInput);

    layout->addLayout(createForm);

    QHBoxLayout *buttons = new QHBoxLayout();
    QPushButton *createBtn = new QPushButton("Create", tab);
    QPushButton *runBtn = new QPushButton("Run Selected", tab);
    QPushButton *deleteBtn = new QPushButton("Delete Selected", tab);
    QPushButton *refreshBtn = new QPushButton("Refresh", tab);

    buttons->addWidget(createBtn);
    buttons->addWidget(runBtn);
    buttons->addWidget(deleteBtn);
    buttons->addWidget(refreshBtn);
    buttons->addStretch(1);
    layout->addLayout(buttons);

    connect(createBtn, &QPushButton::clicked, this, &MainWindow::onCreateTool);
    connect(refreshBtn, &QPushButton::clicked, this, &MainWindow::onRefreshTools);
    connect(runBtn, &QPushButton::clicked, this, &MainWindow::onRunSelectedTool);
    connect(deleteBtn, &QPushButton::clicked, this, &MainWindow::onDeleteSelectedTool);

    return tab;
}

QWidget *MainWindow::buildResearchTab() {
    QWidget *tab = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(tab);

    QHBoxLayout *queryRow = new QHBoxLayout();
    m_researchInput = new QLineEdit(tab);
    m_researchInput->setPlaceholderText("Enter query for web research...");
    m_researchBtn = new QPushButton("Run Research", tab);
    queryRow->addWidget(m_researchInput, 1);
    queryRow->addWidget(m_researchBtn);

    m_researchOutput = new QTextEdit(tab);
    m_researchOutput->setReadOnly(true);

    layout->addLayout(queryRow);
    layout->addWidget(m_researchOutput, 1);

    connect(m_researchBtn, &QPushButton::clicked, this, &MainWindow::onRunResearch);
    connect(m_researchInput, &QLineEdit::returnPressed, this, &MainWindow::onRunResearch);

    return tab;
}

QWidget *MainWindow::buildSettingsTab() {
    QWidget *tab = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(tab);

    QFormLayout *form = new QFormLayout();

    m_llamaCliInput = new QLineEdit(m_configManager.config().llamaCliPath, tab);
    m_llamaModelInput = new QLineEdit(m_configManager.config().llamaModelPath, tab);
    m_llmProfileCombo = new QComboBox(tab);
    m_llmProfileCombo->addItem("Fast (0.5B)", "fast");
    m_llmProfileCombo->addItem("Balanced (3B)", "balanced");
    m_llmProfileCombo->addItem("Strong (7B)", "strong");
    const int profileIndex = m_llmProfileCombo->findData(m_configManager.config().llmProfile);
    m_llmProfileCombo->setCurrentIndex(profileIndex >= 0 ? profileIndex : 1);
    m_installModelsBtn = new QPushButton("Install/Upgrade Local Models", tab);
    m_whisperCliInput = new QLineEdit(m_configManager.config().whisperCliPath, tab);
    m_whisperModelInput = new QLineEdit(m_configManager.config().whisperModelPath, tab);
    m_piperInput = new QLineEdit(m_configManager.config().piperPath, tab);
    m_piperModelInput = new QLineEdit(m_configManager.config().piperModelPath, tab);
    m_piperConfigInput = new QLineEdit(m_configManager.config().piperConfigPath, tab);
    m_llamaNPredictInput = new QLineEdit(QString::number(m_configManager.config().llamaNPredict), tab);
    m_llamaTemperatureInput = new QLineEdit(QString::number(m_configManager.config().llamaTemperature, 'f', 2), tab);
    m_llamaThreadsInput = new QLineEdit(QString::number(m_configManager.config().llamaThreads), tab);
    m_allowlistInput = new QLineEdit(m_configManager.config().toolAllowlist.join(","), tab);
    m_readOnlyCheck = new QCheckBox(tab);
    m_readOnlyCheck->setChecked(m_configManager.config().readOnlyWorkspace);
    m_researchEnabledCheck = new QCheckBox(tab);
    m_researchEnabledCheck->setChecked(m_configManager.config().researchEnabled);

    form->addRow("llama_cli_path", m_llamaCliInput);
    form->addRow("llama_model_path", m_llamaModelInput);
    form->addRow("llm_profile", m_llmProfileCombo);
    form->addRow("whisper_cli_path", m_whisperCliInput);
    form->addRow("whisper_model_path", m_whisperModelInput);
    form->addRow("piper_path", m_piperInput);
    form->addRow("piper_model_path", m_piperModelInput);
    form->addRow("piper_config_path", m_piperConfigInput);
    form->addRow("llama_n_predict", m_llamaNPredictInput);
    form->addRow("llama_temperature", m_llamaTemperatureInput);
    form->addRow("llama_threads (0=auto)", m_llamaThreadsInput);
    form->addRow("tool_allowlist (comma-separated)", m_allowlistInput);
    form->addRow("read_only_workspace", m_readOnlyCheck);
    form->addRow("research_enabled", m_researchEnabledCheck);

    layout->addLayout(form);

    QPushButton *saveBtn = new QPushButton("Save Settings", tab);
    QHBoxLayout *actions = new QHBoxLayout();
    actions->addWidget(saveBtn, 0, Qt::AlignLeft);
    actions->addWidget(m_installModelsBtn, 0, Qt::AlignLeft);
    actions->addStretch(1);
    layout->addLayout(actions);
    layout->addStretch(1);

    connect(saveBtn, &QPushButton::clicked, this, &MainWindow::onSaveSettings);
    connect(m_installModelsBtn, &QPushButton::clicked, this, &MainWindow::onInstallModels);

    return tab;
}

void MainWindow::appendChatLine(const QString &who, const QString &message) {
    const QString stamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    m_chatHistory->append(QString("[%1] %2: %3").arg(stamp, who, message));
}

void MainWindow::setUiBusy(bool busy, const QString &status) {
    m_reasoningBusy = busy;
    m_sendBtn->setEnabled(!busy);
    m_chatInput->setEnabled(!busy);
    if (!m_recording && !m_sttBusy) {
        m_pttBtn->setEnabled(!busy);
    }

    if (!status.isEmpty()) {
        appendChatLine("system", status);
    }
}

void MainWindow::speakAsync(const QString &text) {
    if (!m_speakCheck->isChecked()) {
        return;
    }
    auto f = QtConcurrent::run([this, text]() { m_tts.speak(text); });
    Q_UNUSED(f);
}

void MainWindow::processUserMessage(const QString &text, const QString &speaker) {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    if (m_reasoningBusy) {
        appendChatLine("system", "Still processing previous request.");
        return;
    }

    appendChatLine(speaker, trimmed);

    if (trimmed.startsWith('/')) {
        const QString reply = m_agent->handleUserMessage(trimmed, speaker);
        appendChatLine("robot", reply);
        speakAsync(reply);
        onRefreshMemories();
        return;
    }

    QString mathReply;
    if (trySolveBasicArithmetic(trimmed, &mathReply)) {
        m_memoryStore.addConversation("user", trimmed, speaker, m_agent->sessionId());
        m_memoryStore.addConversation("assistant", mathReply, "robot", m_agent->sessionId());
        appendChatLine("robot", mathReply);
        speakAsync(mathReply);
        onRefreshMemories();
        return;
    }

    m_memoryStore.addConversation("user", trimmed, speaker, m_agent->sessionId());
    const QVector<ConversationEntry> recent = m_memoryStore.recentConversations(10);
    const QVector<MemoryEntry> memories = m_memoryStore.listMemories(8);
    const AppConfig cfg = m_configManager.config();
    const bool autoResearch = cfg.researchEnabled && shouldAutoResearch(trimmed);

    m_pendingReasoningSpeaker = speaker;
    m_pendingReasoningText = trimmed;
    setUiBusy(true, autoResearch ? "Researching and thinking..." : "Thinking...");

    m_reasonerWatcher->setFuture(QtConcurrent::run([this, trimmed, recent, memories, autoResearch]() {
        QString researchSummary;
        if (autoResearch) {
            const ResearchResult result = m_research.search(trimmed);
            if (result.error.isEmpty()) {
                researchSummary = result.summary;
                if (!result.sources.isEmpty()) {
                    researchSummary += "\nSources:";
                    const int maxSources = std::min(3, static_cast<int>(result.sources.size()));
                    for (int i = 0; i < maxSources; ++i) {
                        researchSummary += "\n- " + result.sources[i];
                    }
                }
            }
        }
        return m_reasoner.generateReply(trimmed, recent, memories, researchSummary);
    }));
}

void MainWindow::applyConfigToEngines() {
    const AppConfig &cfg = m_configManager.config();

    m_reasoner.setConfig(cfg);
    m_toolManager.setConfig(cfg);
    m_stt.setConfig(cfg);
    m_tts.setConfig(cfg);
    if (m_agent) {
        m_agent->setResearchEnabled(cfg.researchEnabled);
    }
}

void MainWindow::onSendText() {
    const QString text = m_chatInput->text().trimmed();
    if (text.isEmpty()) {
        return;
    }

    m_chatInput->clear();
    processUserMessage(text, "text-user");
}

void MainWindow::startPushToTalkCapture() {
    if (m_recording) {
        return;
    }

    m_pttBuffer.clear();

    QAudioFormat format;
    format.setSampleRate(16000);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);

    if (m_pttSource) {
        m_pttSource->deleteLater();
        m_pttSource = nullptr;
    }

    m_pttSource = new QAudioSource(format, this);
    m_pttDevice = m_pttSource->start();
    if (!m_pttDevice) {
        appendChatLine("system", "Failed to start microphone capture.");
        return;
    }

    connect(m_pttDevice, &QIODevice::readyRead, this, [this]() {
        if (m_pttDevice) {
            m_pttBuffer.append(m_pttDevice->readAll());
        }
    });

    m_recording = true;
    m_pttBtn->setText("Stop Recording");
}

void MainWindow::stopPushToTalkCapture() {
    if (!m_recording || !m_pttSource) {
        return;
    }

    if (m_pttDevice) {
        m_pttBuffer.append(m_pttDevice->readAll());
    }

    m_pttSource->stop();
    m_pttDevice = nullptr;
    m_recording = false;
    m_pttBtn->setText("Transcribing...");
    m_pttBtn->setEnabled(false);

    if (m_pttBuffer.isEmpty()) {
        m_pttBtn->setText("Push-to-Talk");
        m_pttBtn->setEnabled(!m_reasoningBusy);
        return;
    }

    const VoiceSignature sig = AudioUtils::signatureFromPcm16(m_pttBuffer);
    m_pendingSttSpeaker = m_memoryStore.identifyOrCreateSpeaker(sig);
    const QByteArray audio = m_pttBuffer;
    m_sttBusy = true;

    m_sttWatcher->setFuture(QtConcurrent::run([this, audio]() {
        return m_stt.transcribePcm16Mono(audio, 16000);
    }));
}

void MainWindow::onTogglePushToTalk() {
    if (m_sttBusy) {
        appendChatLine("system", "Speech transcription in progress.");
        return;
    }

    if (!m_recording) {
        startPushToTalkCapture();
    } else {
        stopPushToTalkCapture();
    }
}

void MainWindow::onGgSend() {
    const QString payload = m_ggSendInput->text().trimmed();
    if (payload.isEmpty()) {
        return;
    }

    const QString protocol = m_ggProtocolCombo->currentData().toString();
    const int volume = m_ggVolumeSlider->value();
    const int repeats = m_ggRepeatSpin->value();
    const QString status = m_ggwave.transmitText(payload, volume, protocol, repeats);
    m_ggLog->append("[tx] " + payload);
    m_ggLog->append("[status] " + status);
}

void MainWindow::onToggleGgListen() {
    if (!m_ggwave.isListening()) {
        if (m_ggwave.startListening()) {
            m_ggListenBtn->setText("Stop Listening");
        } else {
            m_ggLog->append("[error] Unable to start ggwave capture.");
        }
    } else {
        m_ggwave.stopListening();
        m_ggListenBtn->setText("Start Listening");
    }
}

void MainWindow::onGgMessageReceived(const QString &message) {
    m_ggLog->append("[rx] " + message);
    if (m_ggRouteToChatCheck->isChecked()) {
        processUserMessage(message, "ggwave-peer");
    }
}

void MainWindow::onRefreshMemories() {
    const QVector<MemoryEntry> items = m_memoryStore.listMemories(200);

    m_memoryTable->setRowCount(items.size());
    for (int i = 0; i < items.size(); ++i) {
        const MemoryEntry &m = items[i];

        m_memoryTable->setItem(i, 0, new QTableWidgetItem(QString::number(m.id)));
        m_memoryTable->setItem(i, 1, new QTableWidgetItem(m.timestamp.toString(Qt::ISODate)));
        m_memoryTable->setItem(i, 2, new QTableWidgetItem(m.category));
        m_memoryTable->setItem(i, 3, new QTableWidgetItem(m.content));
    }

    m_memoryTable->resizeColumnsToContents();
}

void MainWindow::onForgetSelectedMemory() {
    const auto rows = m_memoryTable->selectionModel()->selectedRows();
    if (rows.isEmpty()) {
        return;
    }

    const int row = rows.first().row();
    bool ok = false;
    const int id = m_memoryTable->item(row, 0)->text().toInt(&ok);
    if (!ok) {
        return;
    }

    if (QMessageBox::question(this, "Forget Memory", "Delete memory ID " + QString::number(id) + "?") != QMessageBox::Yes) {
        return;
    }

    if (m_memoryStore.forgetMemory(id)) {
        onRefreshMemories();
    } else {
        QMessageBox::warning(this, "Forget Memory", "Failed to delete memory.");
    }
}

void MainWindow::onCreateTool() {
    ToolManifest t;
    t.name = m_toolNameInput->text().trimmed();
    t.command = m_toolCmdInput->text().trimmed();
    t.description = m_toolDescInput->text().trimmed();
    t.enabled = true;

    const QString argsText = m_toolArgsInput->text().trimmed();
    if (!argsText.isEmpty()) {
        t.arguments = argsText.split(' ', Qt::SkipEmptyParts);
    }

    QString error;
    if (!m_toolManager.createTool(t, &error)) {
        QMessageBox::warning(this, "Create Tool", error);
        return;
    }

    m_toolNameInput->clear();
    m_toolCmdInput->clear();
    m_toolArgsInput->clear();
    m_toolDescInput->clear();

    onRefreshTools();
}

void MainWindow::onRefreshTools() {
    const QVector<ToolManifest> tools = m_toolManager.listTools();

    m_toolsTable->setRowCount(tools.size());
    for (int i = 0; i < tools.size(); ++i) {
        const ToolManifest &t = tools[i];
        m_toolsTable->setItem(i, 0, new QTableWidgetItem(t.name));
        m_toolsTable->setItem(i, 1, new QTableWidgetItem(t.command));
        m_toolsTable->setItem(i, 2, new QTableWidgetItem(t.arguments.join(" ")));
        m_toolsTable->setItem(i, 3, new QTableWidgetItem(t.enabled ? "true" : "false"));
    }

    m_toolsTable->resizeColumnsToContents();
}

void MainWindow::onRunSelectedTool() {
    const auto rows = m_toolsTable->selectionModel()->selectedRows();
    if (rows.isEmpty()) {
        return;
    }

    const QString name = m_toolsTable->item(rows.first().row(), 0)->text();

    const auto confirm = QMessageBox::question(
        this,
        "Run Tool",
        "Run tool '" + name + "'? This executes a local process.");

    if (confirm != QMessageBox::Yes) {
        return;
    }

    const ToolRunResult result = m_toolManager.runTool(name);
    if (!result.error.isEmpty()) {
        QMessageBox::warning(this, "Run Tool", result.error);
        return;
    }

    QMessageBox::information(this,
                             "Run Tool",
                             "Exit code: " + QString::number(result.exitCode) + "\n\n" + result.output);
}

void MainWindow::onDeleteSelectedTool() {
    const auto rows = m_toolsTable->selectionModel()->selectedRows();
    if (rows.isEmpty()) {
        return;
    }

    const QString name = m_toolsTable->item(rows.first().row(), 0)->text();

    if (QMessageBox::question(this, "Delete Tool", "Delete tool '" + name + "'?") != QMessageBox::Yes) {
        return;
    }

    QString error;
    if (!m_toolManager.deleteTool(name, &error)) {
        QMessageBox::warning(this, "Delete Tool", error);
        return;
    }

    onRefreshTools();
}

void MainWindow::onRunResearch() {
    if (!m_configManager.config().researchEnabled) {
        m_researchOutput->setPlainText("Research is disabled in settings.");
        return;
    }

    const QString query = m_researchInput->text().trimmed();
    if (query.isEmpty()) {
        return;
    }

    if (m_researchWatcher->isRunning()) {
        m_researchOutput->setPlainText("Research already running.");
        return;
    }

    m_researchBtn->setEnabled(false);
    m_researchOutput->setPlainText("Running research...");
    m_researchWatcher->setFuture(QtConcurrent::run([this, query]() {
        return m_research.search(query);
    }));
}

void MainWindow::onSaveSettings() {
    AppConfig &cfg = m_configManager.config();

    cfg.llamaCliPath = m_llamaCliInput->text().trimmed();
    cfg.llamaModelPath = m_llamaModelInput->text().trimmed();
    cfg.llmProfile = m_llmProfileCombo->currentData().toString();
    cfg.whisperCliPath = m_whisperCliInput->text().trimmed();
    cfg.whisperModelPath = m_whisperModelInput->text().trimmed();
    cfg.piperPath = m_piperInput->text().trimmed();
    cfg.piperModelPath = m_piperModelInput->text().trimmed();
    cfg.piperConfigPath = m_piperConfigInput->text().trimmed();
    cfg.llamaNPredict = std::max(1, m_llamaNPredictInput->text().trimmed().toInt());
    cfg.llamaTemperature = std::max(0.0, m_llamaTemperatureInput->text().trimmed().toDouble());
    cfg.llamaThreads = std::max(0, m_llamaThreadsInput->text().trimmed().toInt());
    cfg.readOnlyWorkspace = m_readOnlyCheck->isChecked();
    cfg.researchEnabled = m_researchEnabledCheck->isChecked();
    cfg.speakResponses = m_speakCheck->isChecked();
    cfg.ggwaveTxVolume = m_ggVolumeSlider->value();
    cfg.ggwaveProtocol = m_ggProtocolCombo->currentData().toString();
    cfg.ggwaveRepeatCount = m_ggRepeatSpin->value();
    cfg.ggwaveRouteToChat = m_ggRouteToChatCheck->isChecked();

    cfg.toolAllowlist = m_allowlistInput->text().split(',', Qt::SkipEmptyParts);
    for (QString &item : cfg.toolAllowlist) {
        item = item.trimmed();
    }

    if (cfg.toolAllowlist.isEmpty()) {
        cfg.toolAllowlist = {"python", "powershell", "cmd", "git"};
    }

    if (!m_configManager.save(AppPaths::configPath())) {
        QMessageBox::warning(this, "Save Settings", "Failed to save settings file.");
        return;
    }

    applyConfigToEngines();
    QMessageBox::information(this, "Save Settings", "Settings saved.");
}

void MainWindow::onInstallModels() {
    if (m_modelInstallBusy) {
        appendChatLine("system", "Model installation already running.");
        return;
    }

    const QString profile = m_llmProfileCombo->currentData().toString();
    m_modelInstallBusy = true;
    m_installModelsBtn->setEnabled(false);
    m_installModelsBtn->setText("Installing models...");
    appendChatLine("system", "Starting local model install for profile: " + profile);

    m_modelInstallWatcher->setFuture(QtConcurrent::run([profile]() -> QString {
        QProcess process;
        process.setWorkingDirectory(AppPaths::rootDir());
        process.setProgram("powershell");
        process.setArguments({
            "-NoProfile",
            "-ExecutionPolicy",
            "Bypass",
            "-File",
            QDir(AppPaths::rootDir()).filePath("scripts/install_prereqs.ps1"),
            "-ModelsOnly",
            "-LlmProfile",
            profile
        });

        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("ROBOT_LLM_PROFILE", profile);
        process.setProcessEnvironment(env);
        process.start();

        if (!process.waitForStarted(5000)) {
            return QString("ERR\nFailed to start installer process.");
        }

        if (!process.waitForFinished(3600000)) {
            process.kill();
            process.waitForFinished(5000);
            return QString("ERR\nModel install timed out.");
        }

        const QString output = QString::fromUtf8(process.readAllStandardOutput()) +
                               QString::fromUtf8(process.readAllStandardError());

        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            return QString("ERR\n") + output;
        }

        return QString("OK\n") + output;
    }));
}

void MainWindow::onModelInstallFinished() {
    m_modelInstallBusy = false;
    m_installModelsBtn->setEnabled(true);
    m_installModelsBtn->setText("Install/Upgrade Local Models");

    const QString result = m_modelInstallWatcher->result();
    if (result.startsWith("OK\n")) {
        appendChatLine("system", "Local models installed successfully.");
        QMessageBox::information(this, "Model Install", "Local model install completed successfully.");
        return;
    }

    QString details = result;
    if (details.startsWith("ERR\n")) {
        details = details.mid(4);
    }
    if (details.trimmed().isEmpty()) {
        details = "Unknown model install error.";
    }
    appendChatLine("system", "Local model install failed.");
    QMessageBox::warning(this, "Model Install", details);
}

void MainWindow::onReasoningFinished() {
    QString reply = m_reasonerWatcher->result().trimmed();
    if (reply.isEmpty()) {
        reply = "I could not generate a response.";
    }

    m_memoryStore.addConversation("assistant", reply, "robot", m_agent->sessionId());
    appendChatLine("robot", reply);
    onRefreshMemories();
    setUiBusy(false);
    speakAsync(reply);
}

void MainWindow::onSttFinished() {
    m_sttBusy = false;
    m_pttBtn->setText("Push-to-Talk");
    m_pttBtn->setEnabled(!m_reasoningBusy);

    const QString transcript = m_sttWatcher->result().trimmed();
    if (transcript.isEmpty()) {
        appendChatLine("system", "No speech recognized.");
        return;
    }

    processUserMessage(transcript, m_pendingSttSpeaker);
}

void MainWindow::onResearchFinished() {
    m_researchBtn->setEnabled(true);
    const ResearchResult result = m_researchWatcher->result();

    if (!result.error.isEmpty()) {
        m_researchOutput->setPlainText("Research error: " + result.error);
        return;
    }

    QString text = "Summary:\n" + result.summary + "\n\n";
    if (!result.sources.isEmpty()) {
        text += "Sources:\n";
        for (const QString &source : result.sources) {
            text += "- " + source + "\n";
        }
    }

    m_researchOutput->setPlainText(text);
}
