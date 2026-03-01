#pragma once

#include "core/AgentController.h"
#include "core/ConfigManager.h"
#include "core/GgWaveService.h"
#include "core/MemoryStore.h"
#include "core/ReasonerEngine.h"
#include "core/ResearchService.h"
#include "core/SttEngine.h"
#include "core/ToolManager.h"
#include "core/TtsEngine.h"

#include <QMainWindow>

class QAudioSource;
class QCheckBox;
class QComboBox;
class QFutureWatcherBase;
class QLineEdit;
class QPushButton;
class QSlider;
class QSpinBox;
class QTableWidget;
class QTextEdit;
class QTabWidget;
class QIODevice;
template <typename T>
class QFutureWatcher;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onSendText();
    void onTogglePushToTalk();
    void onGgSend();
    void onToggleGgListen();
    void onGgMessageReceived(const QString &message);

    void onRefreshMemories();
    void onForgetSelectedMemory();

    void onCreateTool();
    void onRefreshTools();
    void onRunSelectedTool();
    void onDeleteSelectedTool();
    void onRunResearch();

    void onSaveSettings();
    void onInstallModels();
    void onReasoningFinished();
    void onSttFinished();
    void onResearchFinished();
    void onModelInstallFinished();

private:
    void setupUi();
    QWidget *buildChatTab();
    QWidget *buildSoundTab();
    QWidget *buildMemoryTab();
    QWidget *buildToolsTab();
    QWidget *buildResearchTab();
    QWidget *buildSettingsTab();

    void appendChatLine(const QString &who, const QString &message);
    void applyConfigToEngines();
    void processUserMessage(const QString &text, const QString &speaker);
    void setUiBusy(bool busy, const QString &status = QString());
    void speakAsync(const QString &text);

    void startPushToTalkCapture();
    void stopPushToTalkCapture();

    ConfigManager m_configManager;
    MemoryStore m_memoryStore;
    ReasonerEngine m_reasoner;
    ResearchService m_research;
    ToolManager m_toolManager;
    SttEngine m_stt;
    TtsEngine m_tts;
    GgWaveService m_ggwave;
    AgentController *m_agent = nullptr;

    QTabWidget *m_tabs = nullptr;

    QTextEdit *m_chatHistory = nullptr;
    QLineEdit *m_chatInput = nullptr;
    QPushButton *m_sendBtn = nullptr;
    QPushButton *m_pttBtn = nullptr;
    QCheckBox *m_speakCheck = nullptr;

    QLineEdit *m_ggSendInput = nullptr;
    QPushButton *m_ggSendBtn = nullptr;
    QPushButton *m_ggListenBtn = nullptr;
    QComboBox *m_ggProtocolCombo = nullptr;
    QSlider *m_ggVolumeSlider = nullptr;
    QSpinBox *m_ggRepeatSpin = nullptr;
    QCheckBox *m_ggRouteToChatCheck = nullptr;
    QTextEdit *m_ggLog = nullptr;

    QTableWidget *m_memoryTable = nullptr;

    QTableWidget *m_toolsTable = nullptr;
    QLineEdit *m_toolNameInput = nullptr;
    QLineEdit *m_toolCmdInput = nullptr;
    QLineEdit *m_toolArgsInput = nullptr;
    QLineEdit *m_toolDescInput = nullptr;

    QLineEdit *m_researchInput = nullptr;
    QPushButton *m_researchBtn = nullptr;
    QTextEdit *m_researchOutput = nullptr;

    QLineEdit *m_llamaCliInput = nullptr;
    QLineEdit *m_llamaModelInput = nullptr;
    QComboBox *m_llmProfileCombo = nullptr;
    QPushButton *m_installModelsBtn = nullptr;
    QLineEdit *m_whisperCliInput = nullptr;
    QLineEdit *m_whisperModelInput = nullptr;
    QLineEdit *m_piperInput = nullptr;
    QLineEdit *m_piperModelInput = nullptr;
    QLineEdit *m_piperConfigInput = nullptr;
    QLineEdit *m_llamaNPredictInput = nullptr;
    QLineEdit *m_llamaTemperatureInput = nullptr;
    QLineEdit *m_llamaThreadsInput = nullptr;
    QLineEdit *m_allowlistInput = nullptr;
    QCheckBox *m_readOnlyCheck = nullptr;
    QCheckBox *m_researchEnabledCheck = nullptr;

    QAudioSource *m_pttSource = nullptr;
    QIODevice *m_pttDevice = nullptr;
    QByteArray m_pttBuffer;
    bool m_recording = false;
    bool m_reasoningBusy = false;
    bool m_sttBusy = false;
    QString m_pendingReasoningSpeaker;
    QString m_pendingReasoningText;
    QString m_pendingSttSpeaker;
    QFutureWatcher<QString> *m_reasonerWatcher = nullptr;
    QFutureWatcher<QString> *m_sttWatcher = nullptr;
    QFutureWatcher<ResearchResult> *m_researchWatcher = nullptr;
    QFutureWatcher<QString> *m_modelInstallWatcher = nullptr;
    bool m_modelInstallBusy = false;
};
