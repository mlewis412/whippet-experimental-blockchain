#pragma once
#include <QtWidgets>
#include "rpc.h"

class Desktop : public QMainWindow {
public:
    Desktop(const QString& dataRoot, const QString& networkOverride = {}, bool smoke = false);
    ~Desktop() override;
protected:
    void closeEvent(QCloseEvent* event) override;
private:
    Rpc rpc;
    QProcess node;
    QTimer refreshTimer, sessionTimer;
    QSettings settings;
    std::unique_ptr<QLockFile> instanceLock;
    QString root, network, dataDir, payout, pendingRestore;
    bool ready = false, miningWanted = false, miningPending = false, closing = false;
    bool restarting = false, transactionBusy = false, refreshPending = false;
    int peers = 0, height = 0, found = 0, generation = 0;
    double balance = 0, immature = 0, sessionRewards = 0;
    bool initialDownload = true;
    QElapsedTimer elapsed;
    QStackedWidget* pages;
    QList<QPushButton*> tabs;
    QLabel *connectionLabel, *footerLabel, *notice, *miningTitle, *miningSubtitle;
    QLabel *blocksValue, *rewardValue, *sessionValue, *payoutValue, *balanceValue;
    QLabel *walletBalance, *walletPending, *nodeTitle, *nodeSummary, *heightValue, *peerValue, *hashValue;
    QLabel *storageValue, *networkValue;
    QPushButton *mineButton, *sendButton, *receiveButton;
    QComboBox* pace;
    QProgressBar* syncProgress;
    QTableWidget* history;
    QJsonObject chainInfo, walletInfo;
    QJsonArray transactions;
    QWidget* buildMine();
    QWidget* buildWallet();
    QWidget* buildNode();
    void buildUi();
    void applyTheme();
    void chooseNetwork();
    void startNode();
    void refresh();
    void updateUi();
    void toggleMining();
    void mineBatch();
    void changePayout();
    void sendCoins();
    void receiveCoins();
    void showSettings();
    void backupWallet();
    void walletSecurity();
    void showPeers();
    void inspect();
    void console();
    void showLogs();
    void exportHistory();
    void restartNode();
    void stopNode();
    void error(const QString& text);
    void jsonDialog(const QString& title, const QString& method, const QJsonArray& params = {});
    QString nodeProgram() const;
};
