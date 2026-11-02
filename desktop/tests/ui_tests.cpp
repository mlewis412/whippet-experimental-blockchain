#include <QtTest>
#include "desktop.h"

class UiTests : public QObject {
    Q_OBJECT
private:
    QPushButton* named(QWidget& window,const QString& text) {
        for(auto* b:window.findChildren<QPushButton*>())if(b->text()==text)return b;
        return nullptr;
    }
private slots:
    void compactWindowAndMiningLifecycle() {
        qApp->setQuitOnLastWindowClosed(false);qApp->setStyle("Fusion");
        QTemporaryDir temporary; QVERIFY(temporary.isValid());
        Desktop window(temporary.path(),"regtest");window.show();
        auto cleanup=qScopeGuard([&]{window.close();QElapsedTimer wait;wait.start();while(window.isVisible()&&wait.elapsed()<30000){QCoreApplication::processEvents();QTest::qWait(20);}});
        auto* start=named(window,"Start mining");QVERIFY(start);
        QTRY_VERIFY_WITH_TIMEOUT(start->isEnabled(),40000);
        QVERIFY(window.width()<=800);QVERIFY(window.size().height()<=570);
        const QString screenshots=qEnvironmentVariable("WHIPPET_SCREENSHOTS",QCoreApplication::applicationDirPath()+"/screenshots");QDir().mkpath(screenshots);
        QVERIFY(window.grab().save(screenshots+"/silver-idle.png"));
        QTest::mouseClick(start,Qt::LeftButton);
        QTRY_COMPARE(start->text(),QString("Stop mining"));
        QTRY_VERIFY_WITH_TIMEOUT([&]{for(auto* text:window.findChildren<QLabel*>())if(text->objectName()=="value"&&text->text().toInt()>=22)return true;return false;}(),45000);
        QTRY_VERIFY([&]{for(auto* text:window.findChildren<QLabel*>())if(text->objectName()=="value"&&text->text().endsWith(" WHT")&&text->text().section(' ',0,0).toDouble()>0)return true;return false;}());
        QVERIFY(window.grab().save(screenshots+"/silver-mining.png"));
        QTest::mouseClick(start,Qt::LeftButton);
        QTRY_COMPARE_WITH_TIMEOUT(start->text(),QString("Start mining"),15000);
        QVERIFY(start->isEnabled());
        auto* node=named(window,"Node");QVERIFY(node);QTest::mouseClick(node,Qt::LeftButton);QVERIFY(node->isChecked());
        QVERIFY(window.grab().save(screenshots+"/node.png"));
        auto* wallet=named(window,"Wallet");QVERIFY(wallet);QTest::mouseClick(wallet,Qt::LeftButton);QVERIFY(wallet->isChecked());
        QVERIFY(window.grab().save(screenshots+"/wallet.png"));
        QTimer::singleShot(100,[&]{for(auto* w:qApp->topLevelWidgets())if(auto* d=qobject_cast<QDialog*>(w))if(d->windowTitle()=="Settings"){
            auto combos=d->findChildren<QComboBox*>();QVERIFY(!combos.isEmpty());combos.first()->setCurrentIndex(1);auto* box=d->findChild<QDialogButtonBox*>();QVERIFY(box);QTest::mouseClick(box->button(QDialogButtonBox::Save),Qt::LeftButton);
        }});
        QTest::mouseClick(named(window,"Settings"),Qt::LeftButton);
        QTest::mouseClick(named(window,"Mine"),Qt::LeftButton);
        QVERIFY(window.grab().save(screenshots+"/charcoal.png"));
        window.close();QTRY_VERIFY_WITH_TIMEOUT(!window.isVisible(),30000);
    }
};
QTEST_MAIN(UiTests)
#include "ui_tests.moc"
