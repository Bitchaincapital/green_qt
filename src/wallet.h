#ifndef GREEN_WALLET_H
#define GREEN_WALLET_H

#include <QtQml>
#include <QAtomicInteger>
#include <QList>
#include <QObject>
#include <QQmlListProperty>
#include <QThread>
#include <QJsonObject>

class Account;
class Asset;
class Device;
class Network;

struct GA_session;
struct GA_auth_handler;
struct GA_json;

class Wallet : public QObject
{
    Q_OBJECT
    QML_ELEMENT
public:
    enum ConnectionStatus {
        Disconnected,
        Connecting,
        Connected
    };
    Q_ENUM(ConnectionStatus)

    enum AuthenticationStatus {
        Unauthenticated,
        Authenticating,
        Authenticated
    };
    Q_ENUM(AuthenticationStatus)

private:
    Q_PROPERTY(Network* network READ network WRITE setNetwork NOTIFY networkChanged)
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(ConnectionStatus connection READ connection NOTIFY connectionChanged)
    Q_PROPERTY(AuthenticationStatus authentication READ authentication NOTIFY authenticationChanged)
    Q_PROPERTY(bool authenticated READ isAuthenticated NOTIFY authenticationChanged)
    Q_PROPERTY(QString proxy READ proxy NOTIFY proxyChanged)
    Q_PROPERTY(bool useTor READ useTor NOTIFY useTorChanged)
    Q_PROPERTY(bool locked READ isLocked NOTIFY lockedChanged)
    Q_PROPERTY(QJsonObject settings READ settings NOTIFY settingsChanged)
    Q_PROPERTY(QJsonObject currencies READ currencies CONSTANT)
    Q_PROPERTY(QQmlListProperty<Account> accounts READ accounts NOTIFY accountsChanged)
    Q_PROPERTY(QJsonObject events READ events NOTIFY eventsChanged)
    Q_PROPERTY(QStringList mnemonic READ mnemonic CONSTANT)
    Q_PROPERTY(int loginAttemptsRemaining READ loginAttemptsRemaining NOTIFY loginAttemptsRemainingChanged)
    Q_PROPERTY(QJsonObject config READ config NOTIFY configChanged)
    Q_PROPERTY(bool busy READ isBusy NOTIFY busyChanged)
    Q_PROPERTY(Account* currentAccount READ currentAccount WRITE setCurrentAccount NOTIFY currentAccountChanged)
    // Check if this wallet can create a liquid securities account
    // (only 1 per liquid wallet)
    Q_PROPERTY(bool hasLiquidSecurities READ hasLiquidSecurities NOTIFY hasLiquidSecuritiesChanged)
    Q_PROPERTY(QString networkName READ networkName NOTIFY networkChanged)
    Q_PROPERTY(Device* device READ device CONSTANT)

public:
    explicit Wallet(QObject *parent = nullptr);
    virtual ~Wallet();

    Network* network() const { return m_network; }
    void setNetwork(Network* network);

    QString name() const { return m_name; }
    void setName(const QString& name);

    ConnectionStatus connection() const { return m_connection; }
    AuthenticationStatus authentication() const { return m_authentication; }
    bool isAuthenticated() const { return m_authentication == Authenticated; }

    QString proxy() const { return m_proxy; }
    bool useTor() const { return m_use_tor; }
    bool isLocked() const { return m_locked; }
    void setLocked(bool locked);

    QJsonObject settings() const;
    QJsonObject currencies() const;

    QQmlListProperty<Account> accounts();

    void handleNotification(const QJsonObject& notification);

    QJsonObject events() const;

    QStringList mnemonic() const;

    int loginAttemptsRemaining() const { return m_login_attempts_remaining; }

    QJsonObject config() const { return m_config; }

    Q_INVOKABLE void login(const QStringList& mnemonic, const QString& password = QString());
    Q_INVOKABLE void setPin(const QByteArray& pin);

    Q_INVOKABLE void loginWithPin(const QByteArray& pin);
    Q_INVOKABLE void changePin(const QByteArray& pin);
    Q_INVOKABLE QJsonObject convert(const QJsonObject& value) const;

    qint64 amountToSats(const QString& amount) const;
    Q_INVOKABLE qint64 parseAmount(const QString& amount, const QString& unit) const;

    QString formatAmount(qint64 amount, bool include_ticker) const;
    Q_INVOKABLE QString formatAmount(qint64 amount, bool include_ticker, const QString& unit) const;

    Q_INVOKABLE Asset* getOrCreateAsset(const QString& id);

    bool isBusy() const { return m_busy; }
    void setBusy(bool busy);

    bool hasLiquidSecurities() const { return m_has_liquid_securities; }

    Account* getOrCreateAccount(int pointer);

    void createSession();
    void setSession();

    Device* device() const { return m_device; }
public slots:
    void connect(const QString& proxy, bool use_tor);
    void disconnect();
    void signup(const QStringList &mnemonic, const QByteArray& pin);
    void reload();

    void updateConfig();
    void updateSettings();

    void refreshAssets();

signals:
    void networkChanged(Network* network);
    void connectionChanged();
    void authenticationChanged();
    void proxyChanged(const QString& proxy);
    void useTorChanged(bool use_tor);
    void lockedChanged(bool locked);
    void accountsChanged();
    void eventsChanged(QJsonObject events);
    void nameChanged(QString name);
    void loginAttemptsRemainingChanged(int loginAttemptsRemaining);
    void settingsChanged();
    void configChanged();
    void busyChanged(bool busy);
    void hasLiquidSecuritiesChanged(bool hasLiquidSecurities);
    void currentAccountChanged(Account* account);

protected:
    bool eventFilter(QObject* object, QEvent* event) override;
    void timerEvent(QTimerEvent* event) override;

private:
    void setConnection(ConnectionStatus connection);
    void setAuthentication(AuthenticationStatus authentication);
    void setSettings(const QJsonObject& settings);
    void connectNow();
    void updateCurrencies();

    Account* m_current_account{nullptr};
    QString m_networkName;

public:
    QString m_id;
    QThread* m_thread{nullptr};
    QObject* m_context{nullptr};
    QAtomicInteger<qint64> m_last_timestamp;
    GA_session* m_session{nullptr};
    ConnectionStatus m_connection{Disconnected};
    AuthenticationStatus m_authentication{Unauthenticated};
    bool m_locked{true};
    QJsonObject m_settings;
    QJsonObject m_config;
    QJsonObject m_currencies;
    QJsonObject m_events;
    QMap<QString, Asset*> m_assets;
    QList<Account*> m_accounts;
    QMap<int, Account*> m_accounts_by_pointer;

    QByteArray getPinData() const;
    QByteArray m_pin_data;
    QString m_name;
    Network* m_network{nullptr};
    int m_login_attempts_remaining{3};
    QString m_proxy;
    bool m_use_tor{false};
    int m_logout_timer{-1};
    bool m_busy{false};
    bool m_has_liquid_securities{false};

    void save();
    Account* currentAccount() const { return m_current_account; }
    void setCurrentAccount(Account* account);
    QString networkName() const;
    Device* m_device{nullptr};
};

#endif // GREEN_WALLET_H
