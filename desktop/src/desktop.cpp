#include "desktop.h"
#include "model.h"
#include "qrcodegen.hpp"
#include <stdexcept>

namespace {
class PaymentDialog : public QDialog {
public:
    PaymentDialog(QWidget* parent, std::function<bool()> pending) : QDialog(parent), pending(std::move(pending)) {}
    void reject() override { if (!pending()) QDialog::reject(); }
protected:
    void closeEvent(QCloseEvent* e) override { if(pending())e->ignore();else QDialog::closeEvent(e); }
private:
    std::function<bool()> pending;
};
QLabel* label(const QString& text, const char* role = "", QWidget* parent = nullptr) {
    auto* l = new QLabel(text, parent); l->setObjectName(role); l->setTextFormat(Qt::PlainText); return l;
}
QPushButton* button(const QString& text, const char* role = "") {
    auto* b = new QPushButton(text); b->setObjectName(role); b->setCursor(Qt::PointingHandCursor); return b;
}
QFrame* line() { auto* f = new QFrame; f->setObjectName("divider"); f->setFixedHeight(1); return f; }
QVBoxLayout* pageLayout(QWidget* w) { auto* l = new QVBoxLayout(w); l->setContentsMargins(24,20,24,16); l->setSpacing(14); return l; }
QString pretty(const QJsonValue& v) {
    if (v.isObject()) return QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Indented));
    if (v.isArray()) return QString::fromUtf8(QJsonDocument(v.toArray()).toJson(QJsonDocument::Indented));
    if (v.isString()) return v.toString();
    return QString::fromUtf8(QJsonDocument(QJsonArray{v}).toJson(QJsonDocument::Compact)).mid(1).chopped(1);
}
QWidget* stat(const QString& title, QLabel*& value) {
    auto* w = new QWidget; auto* l = new QVBoxLayout(w); l->setContentsMargins(0,0,0,0); l->setSpacing(5);
    l->addWidget(label(title,"muted")); value = label("—", "value"); l->addWidget(value); return w;
}
QHBoxLayout* row(QVBoxLayout* layout, const QString& title) {
    auto* r = new QHBoxLayout; r->setSpacing(12); r->addWidget(label(title)); layout->addLayout(r); return r;
}
void finishDialog(QDialog* d, QVBoxLayout* l) {
    auto* box = new QDialogButtonBox(QDialogButtonBox::Close); l->addWidget(box);
    QObject::connect(box, &QDialogButtonBox::rejected, d, &QDialog::reject);
}
}

Desktop::Desktop(const QString& dataRoot, const QString& networkOverride, bool smoke, int portOverride)
    : rpc(this), node(this), refreshTimer(this), sessionTimer(this),
      settings(dataRoot + "/desktop.ini", QSettings::IniFormat), root(dataRoot), nodePortOverride(portOverride) {
    QDir().mkpath(root);
    instanceLock = std::make_unique<QLockFile>(root + "/desktop.lock");
    if (!instanceLock->tryLock(0)) throw std::runtime_error("Whippet is already open for this data folder.");
    network = networkOverride.isEmpty() ? settings.value("network", "main").toString() : networkOverride;
    if (!QStringList{"main","test","regtest"}.contains(network)) network = "main";
    if (!networkOverride.isEmpty()) settings.setValue("network", network);
    buildUi(); applyTheme();
    if (!settings.contains("initialized") && networkOverride.isEmpty()) chooseNetwork();
    settings.setValue("initialized", true);
    updateUi();
    connect(&node, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        ready = false; error("The bundled node could not start. Open Settings to check its location."); updateUi();
    });
    connect(&node, qOverload<int,QProcess::ExitStatus>(&QProcess::finished), this, [this](int code, QProcess::ExitStatus) {
        ready = false; miningWanted = false; updateUi();
        if (closing) { QTimer::singleShot(0, this, &QWidget::close); return; }
        if (restarting) { restarting = false; startNode(); return; }
        if (code != 0) error("The node stopped. Open Node → Logs for details, then restart it from Settings.");
    });
    connect(&refreshTimer, &QTimer::timeout, this, &Desktop::refresh);
    connect(&sessionTimer, &QTimer::timeout, this, [this] { if ((miningWanted||miningPending)&&elapsed.isValid()) sessionValue->setText(Model::duration(elapsed.elapsed()/1000)); });
    refreshTimer.start(3000); sessionTimer.start(1000);
    QTimer::singleShot(100, this, &Desktop::startNode);
    if (smoke) QTimer::singleShot(15000, this, &QWidget::close);
}

Desktop::~Desktop() { settings.sync(); }

void Desktop::buildUi() {
    setWindowTitle("Whippet"); resize(780, 550); setMinimumSize(700,500);
    QPixmap icon(64,64); icon.fill(Qt::transparent); QPainter painter(&icon); painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor("#62665e"), 6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPolyline(QPolygonF{{8,18},{20,46},{32,24},{44,46},{56,18}}); painter.end(); setWindowIcon(QIcon(icon));
    auto* central = new QWidget; central->setObjectName("root"); setCentralWidget(central);
    auto* outer = new QVBoxLayout(central); outer->setContentsMargins(0,0,0,0); outer->setSpacing(0);
    auto* nav = new QWidget; nav->setObjectName("navigation"); auto* bar = new QHBoxLayout(nav); bar->setContentsMargins(20,9,20,9); bar->setSpacing(6);
    pages = new QStackedWidget;
    const QStringList names{"Mine", "Wallet", "Node"};
    for (int i=0;i<names.size();++i) {
        auto* b=button(names[i],"tab"); b->setCheckable(true); b->setChecked(i==0); b->setMinimumWidth(78); tabs << b; bar->addWidget(b);
        connect(b,&QPushButton::clicked,this,[this,i] { pages->setCurrentIndex(i); for(int j=0;j<tabs.size();++j) tabs[j]->setChecked(i==j); });
    }
    bar->addStretch(); connectionLabel=label("Starting node", "muted"); bar->addWidget(connectionLabel);
    auto* gear=button("Settings", "quiet"); bar->addWidget(gear); connect(gear,&QPushButton::clicked,this,&Desktop::showSettings);
    outer->addWidget(nav); outer->addWidget(line());
    notice=label("", "notice"); notice->setWordWrap(true); notice->setContentsMargins(24,8,24,8); notice->hide(); outer->addWidget(notice);
    pages->addWidget(buildMine()); pages->addWidget(buildWallet()); pages->addWidget(buildNode()); outer->addWidget(pages,1);
    outer->addWidget(line()); auto* foot=new QHBoxLayout; foot->setContentsMargins(24,8,24,8);
    footerLabel=label("0 peers", "muted"); foot->addWidget(footerLabel);
    auto* details=button("Node details", "link"); foot->addWidget(details); connect(details,&QPushButton::clicked,this,[this]{ tabs[2]->click(); });
    foot->addStretch(); networkValue=label(Model::networkName(network),"muted"); foot->addWidget(networkValue); outer->addLayout(foot);
}

QWidget* Desktop::buildMine() {
    auto* page=new QWidget; auto* l=pageLayout(page);
    auto* heading=new QHBoxLayout; heading->addWidget(label("MINING SESSION","eyebrow")); heading->addStretch();
    auto* tag=label("Scrypt proof of work","muted"); heading->addWidget(tag); l->addLayout(heading);
    auto* panel=new QFrame; panel->setObjectName("panel"); auto* p=new QVBoxLayout(panel); p->setContentsMargins(20,18,20,18); p->setSpacing(16);
    auto* top=new QHBoxLayout; auto* copy=new QVBoxLayout; copy->setSpacing(5);
    miningTitle=label("Ready when you are", "heading"); copy->addWidget(miningTitle);
    miningSubtitle=label("Solo · Scrypt", "muted"); copy->addWidget(miningSubtitle);
    copy->addWidget(label("Payouts go directly to your wallet.","muted")); top->addLayout(copy,1);
    mineButton=button("Start mining","primary"); mineButton->setMinimumWidth(146); mineButton->setMinimumHeight(38); top->addWidget(mineButton);
    connect(mineButton,&QPushButton::clicked,this,&Desktop::toggleMining); p->addLayout(top); p->addWidget(line());
    auto* stats=new QHBoxLayout; stats->addWidget(stat("Blocks found",blocksValue),1); stats->addWidget(stat("Session rewards",rewardValue),1); stats->addWidget(stat("Session",sessionValue),1); p->addLayout(stats); l->addWidget(panel);
    auto* payoutRow=row(l,"Payout address"); payoutValue=label("Choose a payout address","muted"); payoutValue->setTextInteractionFlags(Qt::TextSelectableByMouse); payoutRow->addWidget(payoutValue,1);
    auto* change=button("Change","link"); payoutRow->addWidget(change); connect(change,&QPushButton::clicked,this,&Desktop::changePayout); l->addWidget(line());
    auto* perf=row(l,"Work pace"); pace=new QComboBox; pace->addItems({"Gentle", "Balanced", "Fast"}); pace->setCurrentIndex(settings.value("pace",0).toInt()); pace->setToolTip("Controls the nonce-attempt budget and rest between requests. This is not a CPU percentage limit."); perf->addWidget(pace); perf->addStretch();
    auto* customize=button("Customize","link"); perf->addWidget(customize); connect(customize,&QPushButton::clicked,this,&Desktop::showSettings);
    connect(pace,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int v){ settings.setValue("pace",v); });
    l->addWidget(label("Small work batches keep the interface responsive.","muted")); l->addStretch(); l->addWidget(line());
    auto* wallet=row(l,"Wallet balance"); balanceValue=label("— WHT","walletValue"); wallet->addWidget(balanceValue,1);
    sendButton=button("Send"); receiveButton=button("Receive"); wallet->addWidget(sendButton); wallet->addWidget(receiveButton);
    connect(sendButton,&QPushButton::clicked,this,&Desktop::sendCoins); connect(receiveButton,&QPushButton::clicked,this,&Desktop::receiveCoins);
    return page;
}

QWidget* Desktop::buildWallet() {
    auto* page=new QWidget; auto* l=pageLayout(page); auto* r=new QHBoxLayout;
    r->addWidget(stat("Available balance",walletBalance),1); r->addWidget(stat("Pending / maturing",walletPending),1); l->addLayout(r);
    auto* actions=new QHBoxLayout;
    for (auto pair : QList<QPair<QString,std::function<void()>>>{{"Send",[this]{sendCoins();}},{"Receive",[this]{receiveCoins();}},{"Backup",[this]{backupWallet();}},{"Security",[this]{walletSecurity();}}}) {
        auto* b=button(pair.first); connect(b,&QPushButton::clicked,this,pair.second); actions->addWidget(b);
    }
    actions->addStretch(); auto* csv=button("Export CSV","link"); actions->addWidget(csv); connect(csv,&QPushButton::clicked,this,&Desktop::exportHistory); l->addLayout(actions);
    history=new QTableWidget(0,4); history->setHorizontalHeaderLabels({"When","Type","Amount · WHT","Status"}); history->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    history->verticalHeader()->hide(); history->setEditTriggers(QAbstractItemView::NoEditTriggers); history->setSelectionBehavior(QAbstractItemView::SelectRows);
    history->setShowGrid(false); history->setAlternatingRowColors(false); l->addWidget(history,1);
    l->addWidget(label("Double-click an entry for its transaction details. Showing the latest 100 entries.","muted"));
    connect(history,&QTableWidget::cellDoubleClicked,this,[this](int row,int){ if(row>=0 && row<transactions.size()) jsonDialog("Transaction", "gettransaction",{transactions[row].toObject().value("txid")}); });
    return page;
}

QWidget* Desktop::buildNode() {
    auto* page=new QWidget; auto* l=pageLayout(page); nodeTitle=label("Starting your node", "heading"); l->addWidget(nodeTitle);
    nodeSummary=label("Your wallet and chain data stay on this PC.","muted"); nodeSummary->setWordWrap(true); l->addWidget(nodeSummary);
    syncProgress=new QProgressBar; syncProgress->setRange(0,1000); syncProgress->setTextVisible(false); syncProgress->setFixedHeight(5); l->addWidget(syncProgress); l->addSpacing(8);
    auto* stats=new QHBoxLayout; stats->addWidget(stat("Blocks verified",heightValue),1); stats->addWidget(stat("Connections",peerValue),1); stats->addWidget(stat("Network hashrate · estimate",hashValue),1); l->addLayout(stats); l->addWidget(line());
    storageValue=label("Pruned · 3 GB block-file target", "muted"); l->addWidget(storageValue);
    l->addWidget(label("The storage target applies to block files. Total disk use is higher.","muted"));
    for(auto pair : QList<QPair<QString,std::function<void()>>>{{"Connections & peers  →",[this]{showPeers();}},{"Inspect blocks & transactions  →",[this]{inspect();}},{"RPC & advanced tools  →",[this]{console();}},{"Node logs  →",[this]{showLogs();}}}) {
        auto* b=button(pair.first,"detail"); connect(b,&QPushButton::clicked,this,pair.second); l->addWidget(b);
    }
    l->addStretch(); return page;
}

void Desktop::applyTheme() {
    const bool dark=settings.value("dark",false).toBool();
    const QString bg=dark?"#242523":"#f5f5f2", panel=dark?"#2d2e2b":"#ffffff", text=dark?"#efeee9":"#272923", muted=dark?"#b5b6af":"#73756e", border=dark?"#454640":"#dedfd9", nav=dark?"#2b2c29":"#edeee9", selected=dark?"#454640":"#dadcd5";
    QPalette palette; palette.setColor(QPalette::Window,QColor(bg)); palette.setColor(QPalette::WindowText,QColor(text)); palette.setColor(QPalette::Base,QColor(panel)); palette.setColor(QPalette::AlternateBase,QColor(bg)); palette.setColor(QPalette::Text,QColor(text)); palette.setColor(QPalette::Button,QColor(nav)); palette.setColor(QPalette::ButtonText,QColor(text)); palette.setColor(QPalette::Highlight,QColor(selected)); palette.setColor(QPalette::HighlightedText,QColor(text)); palette.setColor(QPalette::ToolTipBase,QColor(panel)); palette.setColor(QPalette::ToolTipText,QColor(text)); qApp->setPalette(palette);
    QFont font=qApp->font(); font.setPointSizeF(10); qApp->setFont(font);
    qApp->setStyleSheet(QString(R"(
        QWidget { color:%3; } QMainWindow, QDialog, QWidget#root { background:%1; }
        QWidget#navigation { background:%6; } QLabel { background:transparent; }
        QLabel#muted { color:%4; font-size:12px; } QLabel#eyebrow { color:%4; font-size:11px; font-weight:600; letter-spacing:1px; }
        QLabel#heading { font-size:23px; font-weight:600; } QLabel#value { font-size:21px; font-weight:600; }
        QLabel#walletValue { font-size:19px; font-weight:600; } QLabel#notice { background:%6; color:%3; font-size:12px; }
        QFrame#panel { background:%2; border:1px solid %5; border-radius:9px; } QFrame#divider { background:%5; border:none; }
        QPushButton { background:%6; border:1px solid %5; border-radius:6px; padding:7px 15px; min-height:16px; }
        QPushButton:hover { background:%7; } QPushButton:pressed { padding-top:8px; padding-bottom:6px; }
        QPushButton:disabled { color:%4; } QPushButton#tab { border:1px solid transparent; background:transparent; padding:6px 15px; }
        QPushButton#tab:checked { background:%7; border-color:%5; font-weight:600; }
        QPushButton#quiet, QPushButton#link { background:transparent; border:none; padding:5px 8px; }
        QPushButton#link { text-decoration:underline; font-size:12px; } QPushButton#detail { text-align:left; background:transparent; padding:10px 4px; border:0; border-bottom:1px solid %5; border-radius:0; }
        QLineEdit,QPlainTextEdit,QTextEdit,QSpinBox,QComboBox { background:%2; border:1px solid %5; border-radius:5px; padding:6px; selection-background-color:%7; }
        QLineEdit:focus,QPlainTextEdit:focus,QComboBox:focus { border-color:%4; }
        QTableWidget { background:%1; border:none; } QTableWidget::item { padding:8px 4px; border-bottom:1px solid %5; }
        QHeaderView::section { background:%1; border:none; border-bottom:1px solid %5; padding:8px 4px; color:%4; font-weight:400; }
        QProgressBar { background:%5; border:0; border-radius:2px; } QProgressBar::chunk { background:%4; border-radius:2px; }
        QToolTip { background:%2; border:1px solid %5; padding:5px; }
    )").arg(bg,panel,text,muted,border,nav,selected) + (dark ? "QPushButton#primary {background:#e7e6df;color:#252621;border:1px solid #eeede7;font-weight:600;} QPushButton#primary:hover{background:#ffffff;}" : "QPushButton#primary{background:#30332c;color:#ffffff;border:1px solid #30332c;font-weight:600;} QPushButton#primary:hover{background:#44483d;}"));
}

void Desktop::chooseNetwork() {
    QDialog d(this); d.setWindowTitle("Welcome to Whippet"); d.setMinimumWidth(460); auto* l=new QVBoxLayout(&d); l->setContentsMargins(24,24,24,24); l->setSpacing(16);
    l->addWidget(label("Your node. Your wallet.","heading")); auto* explanation=label("Whippet is an experimental network. Choose where to start. You can change this later in Settings.","muted"); explanation->setWordWrap(true); l->addWidget(explanation);
    QComboBox choice; choice.addItems({"Experimental mainnet", "Local test chain — no network needed", "Testnet"}); l->addWidget(&choice);
    auto* note=label("A separate wallet and data folder will be created for this app. Existing Whippet wallets are left untouched.","muted"); note->setWordWrap(true); l->addWidget(note);
    auto* b=button("Set up Whippet","primary"); l->addWidget(b); connect(b,&QPushButton::clicked,&d,&QDialog::accept);
    if(d.exec()!=QDialog::Accepted) throw std::runtime_error("Setup was cancelled. No node was started.");
    network=choice.currentIndex()==1?"regtest":choice.currentIndex()==2?"test":"main"; settings.setValue("network",network);
}

QString Desktop::nodeProgram() const {
    const QString suffix=
#ifdef Q_OS_WIN
        ".exe";
#else
        "";
#endif
    const QString adjacent=QCoreApplication::applicationDirPath()+"/node/whippetd"+suffix;
#if defined(Q_OS_LINUX)
    if(!QFileInfo::exists(adjacent)) return QCoreApplication::applicationDirPath()+"/../libexec/whippet/whippetd";
#endif
    return adjacent;
}

void Desktop::startNode() {
    if(node.state()!=QProcess::NotRunning) return;
    ++generation; miningPending=false; refreshPending=false; ready=false; initialDownload=true; peers=0;
    balance=0; immature=0; chainInfo={}; walletInfo={}; transactions={}; history->setRowCount(0);
    payout=settings.value("payout/"+network).toString();
    dataDir=root+"/chain/"+network; QDir().mkpath(dataDir);
    if(!pendingRestore.isEmpty()) {
        const QString walletDir=dataDir+(network=="regtest"?"/regtest":network=="test"?"/testnet3":"");QDir().mkpath(walletDir);
        const QString destination=walletDir+"/wallet.dat";
        if(QFileInfo::exists(destination)&&!QFile::copy(destination,walletDir+"/before-restore-"+QUuid::createUuid().toString(QUuid::WithoutBraces)+".dat")){error("Could not preserve the current wallet. Restore was not performed.");return;}
        QFile input(pendingRestore);QSaveFile output(destination);
        if(!input.open(QIODevice::ReadOnly)||input.size()==0||!output.open(QIODevice::WriteOnly)){error("Could not open the backup. The current wallet was preserved.");return;}
        bool good=true;while(!input.atEnd()){const auto bytes=input.read(65536);if((bytes.isEmpty()&&input.error()!=QFileDevice::NoError)||output.write(bytes)!=bytes.size()){good=false;break;}}
        if(!good||!output.commit()){error("Could not finish restoring the wallet. The original wallet backup remains in the data folder.");return;}
        pendingRestore.clear();
    }
    QFile conf(dataDir+"/whippet.conf"); if(!conf.exists()) { if(!conf.open(QIODevice::WriteOnly)) {error("Cannot write to the selected data folder.");return;} conf.close(); }
    int port=network=="regtest"?53675:network=="test"?43675:33675;
    if(nodePortOverride>0&&nodePortOverride<=65535)port=nodePortOverride;
    QString cookie=dataDir+(network=="regtest"?"/regtest":network=="test"?"/testnet3":"")+"/.cookie";
    rpc.configure(port,cookie);
    QStringList args{"-server=1","-listen=0","-rpcbind=127.0.0.1","-rpcallowip=127.0.0.1", "-rpcport="+QString::number(port),"-datadir="+dataDir,"-dbcache="+settings.value("cache",96).toString(),"-maxconnections="+settings.value("connections",8).toString(),"-maxuploadtarget="+settings.value("upload",128).toString(),"-prune="+settings.value("prune",3072).toString(),"-uacomment=WhippetDesktop"};
    if(network=="regtest") args<<"-regtest"; else if(network=="test") args<<"-testnet";
    if(!QFileInfo::exists(nodeProgram())) { error("Bundled node is missing. Reinstall Whippet using its desktop installer."); updateUi(); return; }
    node.setProgram(nodeProgram()); node.setArguments(args); node.setWorkingDirectory(QCoreApplication::applicationDirPath());
    node.setProcessChannelMode(QProcess::MergedChannels);
    node.setStandardOutputFile(root+"/node-launch.log",QIODevice::Append);
#ifdef Q_OS_WIN
    node.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* a){ a->flags |= 0x08000000; });
#endif
    node.start(); notice->hide(); updateUi(); QTimer::singleShot(1000,this,&Desktop::refresh);
}

void Desktop::refresh() {
    refreshTimer.setInterval(isMinimized()?12000:3000);
    if(refreshPending || closing || restarting || node.state()==QProcess::NotRunning) return;
    refreshPending=true; const int gen=generation;
    rpc.call("getblockchaininfo",{},[this,gen](QJsonValue value,QString err){
        if(gen!=generation)return; refreshPending=false;
        if(!err.isEmpty()) {ready=false; updateUi(); return;}
        ready=true; chainInfo=value.toObject(); height=chainInfo.value("blocks").toInt(); initialDownload=chainInfo.value("initialblockdownload").toBool(true);
        rpc.call("getnetworkinfo",{},[this,gen](QJsonValue v,QString e){if(gen!=generation||!e.isEmpty())return;peers=v.toObject().value("connections").toInt();updateUi();});
        rpc.call("getwalletinfo",{},[this,gen](QJsonValue v,QString e){if(gen!=generation||!e.isEmpty())return;walletInfo=v.toObject();balance=walletInfo.value("balance").toDouble();immature=walletInfo.value("immature_balance").toDouble();updateUi();});
        rpc.call("listtransactions",{"*",100,0,true},[this,gen](QJsonValue v,QString e){
            if(gen!=generation||!e.isEmpty())return;
            transactions={}; const auto list=v.toArray(); for(int i=list.size()-1;i>=0;--i) transactions.append(list[i]); history->setRowCount(transactions.size());
            for(int i=0;i<transactions.size();++i) {
                auto tx=transactions[i].toObject(); const QString type=tx.value("category").toString(); int confirmations=tx.value("confirmations").toInt();
                QString state=confirmations<0?"Conflicted":type=="immature"?"Maturing":type=="orphan"?"Not accepted":confirmations==0?"Pending":QString("%1 confirmations").arg(confirmations);
                const QStringList cells{QDateTime::fromSecsSinceEpoch(tx.value("time").toVariant().toLongLong()).toString("MMM d, HH:mm"), type=="generate"||type=="immature"?"Mining":type=="send"?"Sent":type=="receive"?"Received":type, Model::coins(tx.value("amount").toDouble()),state};
                for(int j=0;j<cells.size();++j) history->setItem(i,j,new QTableWidgetItem(cells[j]));
            }
        });
        rpc.call("getnetworkhashps",{},[this,gen](QJsonValue v,QString e){if(gen==generation&&e.isEmpty()) {double h=v.toDouble(); hashValue->setText(h>=1e9?QString::number(h/1e9,'f',2)+" GH/s":h>=1e6?QString::number(h/1e6,'f',2)+" MH/s":QString::number(h,'f',0)+" H/s");}});
        if(payout.isEmpty()) rpc.call("getaccountaddress",{"Desktop mining"},[this,gen](QJsonValue v,QString e){if(gen!=generation||!e.isEmpty())return;payout=v.toString();settings.setValue("payout/"+network,payout);updateUi();});
        updateUi();
    });
}

void Desktop::updateUi() {
    const bool running=miningWanted || miningPending;
    connectionLabel->setText(ready?(initialDownload && network!="regtest"?"◌  Syncing":"●  Connected"):"○  Starting node");
    footerLabel->setText(QString("%1 peers").arg(peers)); networkValue->setText(Model::networkName(network));
    miningTitle->setText(miningWanted?"Mining":miningPending?"Finishing current batch":ready?"Ready when you are":"Starting your node");
    miningSubtitle->setText(network=="regtest"?"Solo · Local test chain":"Solo · Scrypt");
    mineButton->setText(miningWanted?"Stop mining":miningPending?"Stopping…":"Start mining");
    mineButton->setEnabled(!closing && !transactionBusy && !restarting && (miningWanted || (!miningPending && Model::canMine(network,initialDownload,peers,ready) && !payout.isEmpty())));
    if(!running && network!="regtest" && ready && (initialDownload||peers==0)) miningTitle->setText(initialDownload?"Waiting for sync":"Waiting for peers");
    blocksValue->setText(QString::number(found)); rewardValue->setText(Model::coins(sessionRewards)+" WHT");
    if(!elapsed.isValid()) sessionValue->setText("—");
    payoutValue->setText(payout.isEmpty()?"Preparing your address…":payout.left(8)+"…"+payout.right(6)); payoutValue->setToolTip(payout);
    balanceValue->setText(ready?Model::coins(balance)+" WHT":"— WHT"); walletBalance->setText(ready?Model::coins(balance)+" WHT":"—");
    walletPending->setText(ready?Model::coins(walletInfo.value("unconfirmed_balance").toDouble())+" / "+Model::coins(immature):"—");
    sendButton->setEnabled(ready&&!transactionBusy); receiveButton->setEnabled(ready);
    nodeTitle->setText(!ready?"Starting your node":network=="regtest"?"Local test chain is ready":initialDownload?"Syncing the blockchain":peers==0?"Looking for peers":"You're up to date");
    nodeSummary->setText(!ready?"Opening the wallet and checking chain data.":network=="regtest"?"Private test coins stay on this PC. Switch networks in Settings.":initialDownload?QString("%1% verified. Mining becomes available after sync.").arg(chainInfo.value("verificationprogress").toDouble()*100,0,'f',2):"Your node is verifying the Whippet network.");
    syncProgress->setValue(network=="regtest"?1000:int(chainInfo.value("verificationprogress").toDouble()*1000));
    heightValue->setText(ready?QLocale().toString(height):"—"); peerValue->setText(QString::number(peers));
    storageValue->setText(settings.value("prune",3072).toInt()?QString("Pruned · %1 MiB block-file target").arg(settings.value("prune",3072).toInt()):"Archive · full blockchain history");
}

void Desktop::error(const QString& text) { notice->setText(text); notice->show(); }

void Desktop::toggleMining() {
    if(miningWanted) { miningWanted=false; updateUi(); return; }
    if(!Model::canMine(network,initialDownload,peers,ready)||payout.isEmpty()||miningPending)return;
    found=0; sessionRewards=0; elapsed.start(); miningWanted=true; notice->hide(); updateUi(); mineBatch();
}

void Desktop::mineBatch() {
    if(!miningWanted||closing||restarting||miningPending)return;
    if(!Model::canMine(network,initialDownload,peers,ready)) {miningWanted=false;error("Mining paused while your node reconnects or syncs.");updateUi();return;}
    miningPending=true; const int gen=generation; const int profile=qBound(0,pace->currentIndex(),2);
    const int budget=profile==0?1000:profile==1?5000:20000;
    // The core's maxtries bounds nonce attempts, not wall-clock validation retries.
    // Stop stops scheduling new work; an outstanding request finishes in the node.
    rpc.call("getblockchaininfo",{},[this,gen,profile,budget](QJsonValue current,QString chainError){
    if(gen!=generation)return;
    if(!chainError.isEmpty()){miningPending=false;miningWanted=false;error(chainError);updateUi();return;}
    height=current.toObject().value("blocks").toInt();
    if(!miningWanted){miningPending=false;updateUi();return;}
    rpc.call("generatetoaddress",{1,payout,budget,Model::needsAuxpow(network,height+1)?1:0},[this,gen,profile](QJsonValue v,QString err){
        if(gen!=generation)return; miningPending=false;
        if(!err.isEmpty()) {miningWanted=false;error("Mining stopped: "+err);updateUi();return;}
        const auto hashes=v.toArray(); found+=hashes.size();
        for(const auto& hash:hashes) rpc.call("getblock",{hash,2},[this,gen](QJsonValue block,QString e){
            if(gen!=generation||!e.isEmpty())return;
            const auto txs=block.toObject().value("tx").toArray(); if(txs.isEmpty())return;
            // Wallet credit excludes immature coinbase rewards. Read the actual
            // coinbase outputs so this session counter is correct before maturity.
            for(const auto& out:txs.first().toObject().value("vout").toArray())sessionRewards+=out.toObject().value("value").toDouble();
            updateUi();
        });
        refresh(); updateUi();
        if(miningWanted) QTimer::singleShot(profile==0?900:profile==1?250:25,this,&Desktop::mineBatch);
    },600000);
    });
}

void Desktop::changePayout() {
    if(!ready)return;
    if(miningWanted||miningPending){error("Stop mining before changing its payout address.");return;}
    bool ok=false; QString address=QInputDialog::getText(this,"Mining payout","Whippet address (leave empty to create a new one)",QLineEdit::Normal,payout,&ok).trimmed();
    if(!ok)return;
    if(address.isEmpty()) rpc.call("getnewaddress",{"Desktop mining"},[this](QJsonValue v,QString e){if(!e.isEmpty()){error(e);return;}payout=v.toString();settings.setValue("payout/"+network,payout);updateUi();});
    else rpc.call("validateaddress",{address},[this,address](QJsonValue v,QString e){if(!e.isEmpty()||!v.toObject().value("isvalid").toBool()){error("That address is not valid for this network.");return;}payout=address;settings.setValue("payout/"+network,payout);updateUi();});
}

void Desktop::receiveCoins() {
    if(!ready)return;
    auto* d=new QDialog(this); d->setWindowTitle("Receive WHT"); d->setMinimumWidth(430); auto* l=new QVBoxLayout(d); l->setContentsMargins(24,24,24,24);l->setSpacing(12);
    l->addWidget(label("Receive", "heading")); auto* qr=new QLabel; qr->setAlignment(Qt::AlignCenter); l->addWidget(qr);
    auto* address=new QLineEdit;address->setReadOnly(true);l->addWidget(address);auto* copy=button("Copy address","primary");l->addWidget(copy);
    auto* requestAmount=new QLineEdit;requestAmount->setPlaceholderText("Optional amount in WHT");l->addWidget(requestAmount);
    auto* request=button("Copy payment request");l->addWidget(request);auto* status=label("Creating a new receiving address…","muted");status->setWordWrap(true);l->addWidget(status);finishDialog(d,l);
    connect(copy,&QPushButton::clicked,d,[address]{if(!address->text().isEmpty())qApp->clipboard()->setText(address->text());});
    auto draw=[address,qr,requestAmount]{
        if(address->text().isEmpty())return;
        QString uri="whippet:"+address->text();qint64 amount;
        if(!requestAmount->text().isEmpty()&&Model::parseAmount(requestAmount->text(),amount))uri+="?amount="+requestAmount->text();
        const auto code=qrcodegen::QrCode::encodeText(uri.toUtf8().constData(),qrcodegen::QrCode::Ecc::MEDIUM);const int border=4,scale=5,size=code.getSize();
        QImage image((size+border*2)*scale,(size+border*2)*scale,QImage::Format_RGB32);image.fill(Qt::white);QPainter p(&image);p.setPen(Qt::NoPen);p.setBrush(Qt::black);
        for(int y=0;y<size;++y)for(int x=0;x<size;++x)if(code.getModule(x,y))p.drawRect((x+border)*scale,(y+border)*scale,scale,scale);p.end();qr->setPixmap(QPixmap::fromImage(image));
    };
    connect(requestAmount,&QLineEdit::textChanged,d,[draw]{draw();});
    connect(request,&QPushButton::clicked,d,[address,requestAmount,status]{qint64 n;QString uri="whippet:"+address->text();if(address->text().isEmpty())return;if(!requestAmount->text().isEmpty()){if(!Model::parseAmount(requestAmount->text(),n)){status->setText("Enter a positive amount with at most 8 decimal places.");return;}uri+="?amount="+requestAmount->text();}qApp->clipboard()->setText(uri);status->setText("Payment request copied.");});
    QPointer<QDialog> guard(d);rpc.call("getnewaddress",{"Desktop receive"},[guard,address,status,draw](QJsonValue v,QString e){if(!guard)return;if(!e.isEmpty()){status->setText(e);return;}address->setText(v.toString());status->setText("Share this address to receive WHT on the selected network.");draw();});
    d->exec();delete d;
}

void Desktop::sendCoins() {
    if(!ready||transactionBusy)return;
    auto* d=new PaymentDialog(this,[this]{return transactionBusy;});d->setWindowTitle("Send WHT");d->setMinimumWidth(460);auto* l=new QVBoxLayout(d);l->setContentsMargins(24,24,24,24);l->setSpacing(12);
    l->addWidget(label("Send", "heading"));auto* to=new QLineEdit;to->setPlaceholderText("Recipient address");to->setObjectName("recipient");auto* amount=new QLineEdit;amount->setPlaceholderText("Amount in WHT");amount->setObjectName("amount");l->addWidget(to);l->addWidget(amount);
    auto* status=label("Review the recipient, amount and fee before sending.","muted");status->setWordWrap(true);l->addWidget(status);
    auto* send=button("Review payment","primary");l->addWidget(send);finishDialog(d,l);QPointer<QDialog> guard(d);
    auto locked=std::make_shared<QJsonArray>();auto unlocked=std::make_shared<bool>(false);
    connect(send,&QPushButton::clicked,d,[this,guard,to,amount,status,send,locked,unlocked]{
        qint64 satoshis;if(!Model::parseAmount(amount->text(),satoshis)){status->setText("Enter a positive amount with at most 8 decimal places.");return;}
        const QString recipient=to->text().trimmed();const double value=double(satoshis)/100000000;
        if(recipient.isEmpty()){status->setText("Enter the recipient's Whippet address.");return;}
        send->setEnabled(false);transactionBusy=true;updateUi();
        rpc.call("validateaddress",{recipient},[this,guard,to,amount,status,send,locked,unlocked,recipient,value](QJsonValue valid,QString err){
            if(!guard){transactionBusy=false;updateUi();return;}
            if(!err.isEmpty()||!valid.toObject().value("isvalid").toBool()){status->setText("That address is not valid for this network.");send->setEnabled(true);transactionBusy=false;updateUi();return;}
            rpc.call("createrawtransaction",{QJsonArray{},QJsonObject{{recipient,value}}},[this,guard,status,send,locked,unlocked,recipient,value](QJsonValue raw,QString e){
                if(!guard){transactionBusy=false;updateUi();return;}
                if(!e.isEmpty()){status->setText(e);send->setEnabled(true);transactionBusy=false;updateUi();return;}
                rpc.call("fundrawtransaction",{raw,QJsonObject{{"lockUnspents",false}}},[this,guard,status,send,locked,unlocked,recipient,value](QJsonValue funded,QString e2){
                    if(!guard){transactionBusy=false;updateUi();return;}
                    if(!e2.isEmpty()){status->setText(e2);send->setEnabled(true);transactionBusy=false;updateUi();return;}
                    const auto f=funded.toObject();const QString hex=f.value("hex").toString();const double fee=f.value("fee").toDouble();
                    rpc.call("decoderawtransaction",{hex},[this,guard,status,send,locked,unlocked,recipient,value,fee,hex](QJsonValue decoded,QString e3){
                        if(!guard){transactionBusy=false;updateUi();return;}
                        if(!e3.isEmpty()){status->setText(e3);send->setEnabled(true);transactionBusy=false;updateUi();return;}
                        const auto choice=QMessageBox::question(guard,"Confirm payment",QString("Recipient:\n%1\n\nAmount: %2 WHT\nFee: %3 WHT\nTotal: %4 WHT\n\nSend on %5?").arg(recipient,Model::coins(value),Model::coins(fee),Model::coins(value+fee),Model::networkName(network)),QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel);
                        if(choice!=QMessageBox::Yes){transactionBusy=false;send->setEnabled(true);updateUi();return;}
                        auto sign=[this,guard,status,send,locked,unlocked,hex]{rpc.call("signrawtransaction",{hex},[this,guard,status,send,locked,unlocked](QJsonValue signedTx,QString se){
                            if(!guard){transactionBusy=false;updateUi();return;}
                            if(!se.isEmpty()||!signedTx.toObject().value("complete").toBool()){status->setText(se.isEmpty()?"The wallet could not fully sign this payment.":se);send->setEnabled(true);transactionBusy=false;updateUi();return;}
                            rpc.call("sendrawtransaction",{signedTx.toObject().value("hex")},[this,guard,status,send,locked,unlocked](QJsonValue txid,QString be){
                                transactionBusy=false;if(*unlocked){rpc.call("walletlock",{},[](QJsonValue,QString){});*unlocked=false;}if(!guard){updateUi();return;}
                                if(!be.isEmpty()){status->setText(be);send->setEnabled(true);}else{*locked=QJsonArray{};status->setText("Payment sent. Transaction: "+txid.toString());send->setText("Sent");}refresh();updateUi();
                            });
                        });};
                        if(walletInfo.contains("unlocked_until")&&walletInfo.value("unlocked_until").toDouble()<=QDateTime::currentSecsSinceEpoch()){
                            bool ok=false;QString pass=QInputDialog::getText(guard,"Unlock wallet","Wallet passphrase",QLineEdit::Password,{},&ok);
                            if(!ok){transactionBusy=false;send->setEnabled(true);updateUi();return;}
                            rpc.call("walletpassphrase",{pass,60},[this,guard,status,send,unlocked,sign](QJsonValue,QString ue){if(!guard){transactionBusy=false;return;}if(!ue.isEmpty()){status->setText(ue);transactionBusy=false;send->setEnabled(true);updateUi();return;}*unlocked=true;sign();});pass.fill(QChar(0));
                        }else sign();
                    });
                });
            });
        });
    });
    // The dialog blocks dismissal during RPC operations. Funding does not lock coins;
    // the node revalidates inputs when broadcasting, and no funds move before confirmation.
    connect(d,&QDialog::finished,this,[this,locked,unlocked]{if(!locked->isEmpty())rpc.call("lockunspent",{true,*locked},[](QJsonValue,QString){});if(*unlocked)rpc.call("walletlock",{},[](QJsonValue,QString){});});
    d->exec();delete d;
}

void Desktop::backupWallet() {
    if(!ready)return;const QString path=QFileDialog::getSaveFileName(this,"Backup wallet",QDir::homePath()+"/whippet-wallet-"+QDate::currentDate().toString("yyyy-MM-dd")+".dat","Wallet backup (*.dat)");if(path.isEmpty())return;
    const QString internal="desktop-"+QUuid::createUuid().toString(QUuid::WithoutBraces)+".dat";
    const QString source=dataDir+(network=="regtest"?"/regtest":network=="test"?"/testnet3":"")+"/backups/"+internal;
    rpc.call("backupwallet",{internal},[this,path,source](QJsonValue,QString e){
        if(!e.isEmpty()){error(e);return;}
        QFile input(source);QSaveFile output(path);
        if(!input.open(QIODevice::ReadOnly)||input.size()==0||!output.open(QIODevice::WriteOnly)){error("The node made a backup, but it could not be saved to the selected location. The internal copy remains in the node's backups folder.");return;}
        bool good=true;while(!input.atEnd()){const QByteArray data=input.read(65536);if(data.isEmpty()&&input.error()!=QFileDevice::NoError){good=false;break;}if(output.write(data)!=data.size()){good=false;break;}}
        if(!good||!output.commit()){error("Could not finish saving the backup. The internal backup is still available in your node data folder.");return;}
        QMessageBox::information(this,"Wallet backup","Wallet backup saved. Keep it somewhere private and secure.");
    });
}

void Desktop::walletSecurity() {
    if(!ready)return;
    QStringList options{"Back up wallet", "Lock wallet", "Encrypt wallet", "Change passphrase", "Sign a message", "Verify a message", "Import watch-only address", "Advanced wallet RPC", "Restore wallet backup"};bool ok=false;
    QString choice=QInputDialog::getItem(this,"Wallet security","Choose an action",options,0,false,&ok);if(!ok)return;
    if(choice==options[0]){backupWallet();return;}if(choice==options[1]){rpc.call("walletlock",{},[this](QJsonValue,QString e){if(!e.isEmpty())error(e);refresh();});return;}
    if(choice==options[7]){console();return;}
    if(choice==options[8]){
        if(miningWanted||miningPending||transactionBusy){error("Stop mining and finish any payment before restoring a wallet.");return;}
        const QString file=QFileDialog::getOpenFileName(this,"Restore wallet backup",QDir::homePath(),"Wallet backup (*.dat)");if(file.isEmpty())return;
        if(QMessageBox::question(this,"Restore wallet",QString("Restore this backup on %1? The current wallet will be preserved as a separate file before replacement. Restoring older wallets may require downloading missing blockchain history.").arg(Model::networkName(network)),QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel)!=QMessageBox::Yes)return;
        pendingRestore=file;restartNode();return;
    }
    if(choice==options[2]||choice==options[3]){
        if(miningWanted||miningPending||transactionBusy){error("Stop mining and finish any payment before changing wallet security.");return;}
        if(choice==options[2]&&walletInfo.contains("unlocked_until")){error("This wallet is already encrypted.");return;}
        QString old;if(choice==options[3]){old=QInputDialog::getText(this,"Wallet security","Current passphrase",QLineEdit::Password,{},&ok);if(!ok)return;}
        QString pass=QInputDialog::getText(this,"Wallet security","New passphrase",QLineEdit::Password,{},&ok);if(!ok||pass.isEmpty())return;
        QString confirm=QInputDialog::getText(this,"Wallet security","Confirm new passphrase",QLineEdit::Password,{},&ok);if(!ok||pass!=confirm){error("Passphrases did not match.");return;}
        if(choice==options[2]){
            if(QMessageBox::question(this,"Encrypt wallet","Keep a safe copy of your passphrase. Losing it means losing access to this wallet. The node will restart after encryption. Continue?",QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel)!=QMessageBox::Yes)return;
            restarting=true;rpc.call("encryptwallet",{pass},[this](QJsonValue,QString e){if(!e.isEmpty()){restarting=false;error(e);}else {notice->setText("Wallet encrypted. Restarting the node…");notice->show();}});
        }else rpc.call("walletpassphrasechange",{old,pass},[this](QJsonValue,QString e){if(!e.isEmpty())error(e);else QMessageBox::information(this,"Wallet security","Passphrase changed.");});
        old.fill(QChar(0));pass.fill(QChar(0));confirm.fill(QChar(0));return;
    }
    QString address=QInputDialog::getText(this,choice,"Whippet address",QLineEdit::Normal,{},&ok).trimmed();if(!ok||address.isEmpty())return;
    if(choice==options[6]){rpc.call("importaddress",{address,"Watch-only",false},[this](QJsonValue,QString e){if(!e.isEmpty())error(e);else QMessageBox::information(this,"Watch-only address","Imported without rescanning. Historical funds may require an archive node and a rescan through advanced tools.");});return;}
    QString message=QInputDialog::getMultiLineText(this,choice,"Message",{},&ok);if(!ok)return;
    if(choice==options[4])rpc.call("signmessage",{address,message},[this](QJsonValue v,QString e){if(!e.isEmpty())error(e);else{QInputDialog dialog(this);dialog.setWindowTitle("Message signature");dialog.setLabelText("Copy this signature");dialog.setTextValue(v.toString());dialog.exec();}});
    else{QString signature=QInputDialog::getText(this,choice,"Signature",QLineEdit::Normal,{},&ok);if(ok)rpc.call("verifymessage",{address,signature,message},[this](QJsonValue v,QString e){if(!e.isEmpty())error(e);else QMessageBox::information(this,"Verify message",v.toBool()?"Signature is valid.":"Signature is not valid.");});}
}

void Desktop::showSettings() {
    auto* d=new QDialog(this);d->setWindowTitle("Settings");d->setMinimumWidth(460);auto* l=new QVBoxLayout(d);l->setContentsMargins(24,24,24,24);l->setSpacing(14);l->addWidget(label("Make yourself at home","heading"));
    auto* form=new QFormLayout;auto* theme=new QComboBox;theme->addItems({"Silver","Charcoal"});theme->setCurrentIndex(settings.value("dark",false).toBool()?1:0);form->addRow("Appearance",theme);
    auto* networks=new QComboBox;networks->addItems({"Experimental mainnet","Testnet","Local test chain"});networks->setCurrentIndex(network=="main"?0:network=="test"?1:2);form->addRow("Network",networks);
    auto* cache=new QSpinBox;cache->setRange(32,4096);cache->setSuffix(" MiB");cache->setValue(settings.value("cache",96).toInt());form->addRow("Database cache",cache);
    auto* prune=new QSpinBox;prune->setRange(0,1048576);prune->setSpecialValueText("Archive");prune->setSuffix(" MiB");prune->setValue(settings.value("prune",3072).toInt());form->addRow("Block-file target",prune);
    auto* connections=new QSpinBox;connections->setRange(1,64);connections->setValue(settings.value("connections",8).toInt());form->addRow("Connection limit",connections);
    auto* upload=new QSpinBox;upload->setRange(0,1048576);upload->setSuffix(" MiB / day");upload->setSpecialValueText("Unlimited");upload->setValue(settings.value("upload",128).toInt());form->addRow("Upload target",upload);l->addLayout(form);
    auto* note=label("Node changes require a restart. Cache is not a total memory cap. Pruning old blocks can prevent historical rescans; switching back to archive requires a full download.","muted");note->setWordWrap(true);l->addWidget(note);
    auto* folder=button("Open data folder","link");l->addWidget(folder);connect(folder,&QPushButton::clicked,d,[this]{QDesktopServices::openUrl(QUrl::fromLocalFile(root));});
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel);l->addWidget(buttons);connect(buttons,&QDialogButtonBox::accepted,d,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,d,&QDialog::reject);
    if(d->exec()==QDialog::Accepted){
        if(prune->value()>0&&prune->value()<2200){error("Choose Archive (0) or a block-file target of at least 2200 MiB.");delete d;return;}
        const QString next=networks->currentIndex()==0?"main":networks->currentIndex()==1?"test":"regtest";
        bool restart=next!=network||cache->value()!=settings.value("cache",96).toInt()||prune->value()!=settings.value("prune",3072).toInt()||connections->value()!=settings.value("connections",8).toInt()||upload->value()!=settings.value("upload",128).toInt();
        if(restart&&(transactionBusy||miningPending||miningWanted)){error("Stop mining and finish any payment before changing node settings.");delete d;return;}
        settings.setValue("dark",theme->currentIndex()==1);settings.setValue("cache",cache->value());settings.setValue("prune",prune->value());settings.setValue("connections",connections->value());settings.setValue("upload",upload->value());settings.setValue("network",next);network=next;applyTheme();
        if(restart)restartNode();else if(node.state()==QProcess::NotRunning)startNode();
    }delete d;
}

void Desktop::jsonDialog(const QString& title,const QString& method,const QJsonArray& params) {
    if(!ready)return;auto* d=new QDialog(this);d->setWindowTitle(title);d->resize(620,420);auto* l=new QVBoxLayout(d);auto* output=new QPlainTextEdit;output->setReadOnly(true);output->setPlainText("Loading…");l->addWidget(output);finishDialog(d,l);QPointer<QDialog> guard(d);
    rpc.call(method,params,[guard,output](QJsonValue v,QString e){if(guard)output->setPlainText(e.isEmpty()?pretty(v):e);});d->exec();delete d;
}

void Desktop::showPeers() {
    if(!ready)return;auto* d=new QDialog(this);d->setWindowTitle("Connections & peers");d->resize(650,420);auto* l=new QVBoxLayout(d);auto* output=new QPlainTextEdit;output->setReadOnly(true);l->addWidget(output);auto* row=new QHBoxLayout;
    auto* address=new QLineEdit;address->setPlaceholderText("host:port");row->addWidget(address);auto* add=button("Add peer");auto* disconnect=button("Disconnect");auto* ban=button("Ban IP");row->addWidget(add);row->addWidget(disconnect);row->addWidget(ban);l->addLayout(row);finishDialog(d,l);QPointer<QDialog> guard(d);
    auto load=[this,guard,output]{rpc.call("getpeerinfo",{},[guard,output](QJsonValue v,QString e){if(guard)output->setPlainText(e.isEmpty()?pretty(v):e);});};
    connect(add,&QPushButton::clicked,d,[this,address,guard,output,load]{rpc.call("addnode",{address->text().trimmed(),"add"},[guard,output,load](QJsonValue,QString e){if(guard){if(!e.isEmpty())output->setPlainText(e);else load();}});});
    connect(disconnect,&QPushButton::clicked,d,[this,address,guard,output,load]{rpc.call("disconnectnode",{address->text().trimmed()},[guard,output,load](QJsonValue,QString e){if(guard){if(!e.isEmpty())output->setPlainText(e);else load();}});});
    connect(ban,&QPushButton::clicked,d,[this,address,guard,output,load]{if(QMessageBox::question(guard,"Ban peer","Ban this IP for 24 hours?",QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel)==QMessageBox::Yes)rpc.call("setban",{address->text().trimmed(),"add",86400},[guard,output,load](QJsonValue,QString e){if(guard){if(!e.isEmpty())output->setPlainText(e);else load();}});});load();d->exec();delete d;
}

void Desktop::inspect() {
    if(!ready)return;bool ok=false;QString query=QInputDialog::getText(this,"Inspect chain","Block height, block hash or transaction ID",QLineEdit::Normal,QString::number(height),&ok).trimmed();if(!ok||query.isEmpty())return;
    bool numeric=false;const int h=query.toInt(&numeric);if(numeric)rpc.call("getblockhash",{h},[this](QJsonValue v,QString e){if(!e.isEmpty())error(e);else jsonDialog("Block","getblock",{v});});
    else rpc.call("getblock",{query},[this,query](QJsonValue v,QString e){if(e.isEmpty())jsonDialog("Block","getblock",{query});else jsonDialog("Transaction (requires available history)","getrawtransaction",{query,true});});
}

void Desktop::console() {
    if(!ready)return;auto* d=new QDialog(this);d->setWindowTitle("Advanced RPC tools");d->resize(650,460);auto* l=new QVBoxLayout(d);auto* note=label("Full node RPC access. Commands can spend funds or change your wallet. Never run commands from someone you do not trust.","muted");note->setWordWrap(true);l->addWidget(note);
    auto* method=new QLineEdit("getblockchaininfo");method->setPlaceholderText("Method");auto* params=new QPlainTextEdit("[]");params->setMaximumHeight(80);l->addWidget(method);l->addWidget(label("Parameters · JSON array","muted"));l->addWidget(params);auto* run=button("Run command");l->addWidget(run);auto* result=new QPlainTextEdit;result->setReadOnly(true);l->addWidget(result,1);finishDialog(d,l);QPointer<QDialog> guard(d);
    connect(run,&QPushButton::clicked,d,[this,guard,method,params,result,run]{QJsonParseError parse;auto doc=QJsonDocument::fromJson(params->toPlainText().toUtf8(),&parse);if(parse.error!=QJsonParseError::NoError||!doc.isArray()){result->setPlainText("Enter a valid JSON array of parameters.");return;}
        const QString name=method->text().trimmed();static const QStringList reads{"help","getblockchaininfo","getnetworkinfo","getwalletinfo","getpeerinfo","getmempoolinfo","getblockcount","getblock","getblockhash","gettransaction","getrawtransaction","listtransactions","listunspent","listbanned","getchaintips","validateaddress","decoderawtransaction","decodescript","getmininginfo","getnetworkhashps"};
        if(!reads.contains(name)&&QMessageBox::question(guard,"Run advanced command",QString("Run %1 on %2? This may change your wallet or node.").arg(name,Model::networkName(network)),QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel)!=QMessageBox::Yes)return;
        run->setEnabled(false);rpc.call(name,doc.array(),[guard,result,run](QJsonValue v,QString e){if(guard){result->setPlainText(e.isEmpty()?pretty(v):e);run->setEnabled(true);}},600000);
    });d->exec();delete d;
}

void Desktop::showLogs() {
    const QString dir=dataDir+(network=="regtest"?"/regtest":network=="test"?"/testnet3":"");QFile file(dir+"/debug.log");QString content;
    if(file.open(QIODevice::ReadOnly)){if(file.size()>128*1024)file.seek(file.size()-128*1024);content=QString::fromUtf8(file.readAll());}else{QFile launch(root+"/node-launch.log");if(launch.open(QIODevice::ReadOnly)){if(launch.size()>128*1024)launch.seek(launch.size()-128*1024);content=QString::fromUtf8(launch.readAll());}}
    content.replace(QRegularExpression("(?i)(rpcpassword|rpcuser|authorization|cookie)\\s*[:=]\\s*[^\\s]+"),"[redacted]");auto* d=new QDialog(this);d->setWindowTitle("Node logs · last 128 KiB");d->resize(700,440);auto* l=new QVBoxLayout(d);auto* text=new QPlainTextEdit(content.isEmpty()?"No node logs yet.":content);text->setReadOnly(true);l->addWidget(text);finishDialog(d,l);d->exec();delete d;
}

void Desktop::exportHistory() {
    const QString path=QFileDialog::getSaveFileName(this,"Export latest 100 transactions",QDir::homePath()+"/whippet-activity.csv","CSV (*.csv)");if(path.isEmpty())return;QFile file(path);if(!file.open(QIODevice::WriteOnly|QIODevice::Text)){error("Could not save that file.");return;}QTextStream stream(&file);stream<<"Time,Type,Amount WHT,Confirmations,Transaction ID\n";
    for(const auto& v:transactions){const auto tx=v.toObject();QStringList fields{QDateTime::fromSecsSinceEpoch(tx.value("time").toVariant().toLongLong()).toString(Qt::ISODate),tx.value("category").toString(),Model::coins(tx.value("amount").toDouble()),QString::number(tx.value("confirmations").toInt()),tx.value("txid").toString()};for(QString& s:fields){s.replace('"',"\"\"");s='"'+s+'"';}stream<<fields.join(',')<<'\n';}
}

void Desktop::restartNode() { if(transactionBusy)return;miningWanted=false;restarting=true;notice->setText("Saving the wallet and restarting your node…");notice->show();if(node.state()==QProcess::NotRunning){restarting=false;startNode();}else stopNode(); }
void Desktop::stopNode() { rpc.call("stop",{},[this](QJsonValue,QString e){if(!e.isEmpty()&&node.state()!=QProcess::NotRunning){error("Waiting for the node to save and stop. Please keep this window open.");if(closing||restarting)QTimer::singleShot(1000,this,&Desktop::stopNode);}},10000); }
void Desktop::closeEvent(QCloseEvent* event) {
    if(transactionBusy){event->ignore();error("Finish the payment before closing Whippet.");return;}
    if(node.state()!=QProcess::NotRunning){event->ignore();if(!closing){closing=true;miningWanted=false;notice->setText("Saving your wallet and stopping the node…");notice->show();updateUi();stopNode();}return;}
    settings.sync();event->accept();
}
