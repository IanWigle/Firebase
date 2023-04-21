#include "pch.h"

FirebaseLib::FirebaseManager::FirebaseManager(const std::string& email, const std::string& password) :
    m_Auth(nullptr),
    m_App(nullptr),
    m_Email(email),
    m_Password(password)
{
}

FirebaseLib::FirebaseManager::FirebaseManager()
{
    m_Email = "";
    m_Password = "";
    m_Auth = nullptr;
    m_App = nullptr;
    SetupApp();
    SetupAuth();
    SetupDatabase();
}

FirebaseLib::FirebaseManager::~FirebaseManager()
{
    Destroy();
}

void FirebaseLib::FirebaseManager::Destroy()
{
    delete m_Database;
    m_Database = nullptr;

    delete m_Auth;
    m_Auth = nullptr;

    delete m_App;
    m_App = nullptr;
}

void FirebaseLib::FirebaseManager::Register()
{
    User = m_Auth->CreateUserWithEmailAndPassword(m_Email.c_str(), m_Password.c_str());
}

void FirebaseLib::FirebaseManager::Login()
{
}

void FirebaseLib::FirebaseManager::SignOut()
{
    m_Auth->SignOut();
}

std::string& FirebaseLib::FirebaseManager::GetEmail()
{
    return m_Email;
}

std::string& FirebaseLib::FirebaseManager::GetPassword()
{
    return m_Password;
}

void FirebaseLib::FirebaseManager::SetEmail(const char* userName)
{
    m_Email = userName;
}

void FirebaseLib::FirebaseManager::SetPassword(const char* password)
{
    m_Password = password;
}

bool FirebaseLib::FirebaseManager::StilSignedIn()
{
    return m_Auth->current_user() != nullptr;
}

void FirebaseLib::FirebaseManager::DeleteCurrentAccount()
{
    firebase::auth::User* user_to_delete = m_Auth->current_user();

    Future<void> delete_future = user_to_delete->Delete();
    WaitForFuture(delete_future, "FirebaseManager::DeleteCurrentAccount()", kAuthErrorNone);
}

void FirebaseLib::FirebaseManager::SignInAnon()
{
    User = m_Auth->SignInAnonymously();
}

int FirebaseLib::FirebaseManager::GetAccountType()
{
    if (m_Auth->current_user() == nullptr)
        return 0;
    else
    {
        if (m_Auth->current_user()->is_anonymous())
        {
            return 1;
        }
        else if (m_Auth->current_user()->is_email_verified())
        {
            return 2;
        }
    }

    return 0;
}

std::string FirebaseLib::FirebaseManager::GetLauncherVersion()
{
    std::string version = "";
    firebase::database::DatabaseReference ref = m_Database->GetReference("ClientData");

    firebase::Future<firebase::database::DataSnapshot> f1 =
        ref.Child("Version").GetValue();

    WaitForCompletion(f1, "Getting client version");

    return f1.result()->value().string_value();
}

void FirebaseLib::FirebaseManager::SetupApp()
{
    ChangeToFileDirectory(
        FIREBASE_CONFIG_STRING);  // NOLINT
#ifdef _WIN32
    SetConsoleCtrlHandler((PHANDLER_ROUTINE)SignalHandler, TRUE);
#else
    signal(SIGINT, SignalHandler);
#endif  // _WIN32

    m_App = App::Create();
    LogMessage("Created the Firebase app %x.",
        static_cast<int>(reinterpret_cast<intptr_t>(m_App)));

    void* initialize_targets[] = { &m_Auth, &m_Database };

    const firebase::ModuleInitializer::InitializerFn initializers[] = {
      [](::firebase::App* app, void* data) {
        LogMessage("Attempt to initialize Firebase Auth.");
        void** targets = reinterpret_cast<void**>(data);
        ::firebase::InitResult result;
        *reinterpret_cast<::firebase::auth::Auth**>(targets[0]) =
            ::firebase::auth::Auth::GetAuth(app, &result);
        return result;
      },
      [](::firebase::App* app, void* data) {
        LogMessage("Attempt to initialize Firebase Database.");
        void** targets = reinterpret_cast<void**>(data);
        ::firebase::InitResult result;
        *reinterpret_cast<::firebase::database::Database**>(targets[1]) =
            ::firebase::database::Database::GetInstance(app, &result);
        return result;
      } };

    ::firebase::ModuleInitializer initializer;
    initializer.Initialize(m_App, initialize_targets, initializers,
        sizeof(initializers) / sizeof(initializers[0]));

    WaitForCompletion(initializer.InitializeLastResult(), "Initialize");
}

void FirebaseLib::FirebaseManager::SetupAuth()
{
    m_Auth = Auth::GetAuth(m_App);
}

void FirebaseLib::FirebaseManager::SetupDatabase()
{
    m_SavedUrl = m_Database->url();
    m_Database->set_persistence_enabled(false);
}
