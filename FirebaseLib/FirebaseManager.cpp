#include "pch.h"
#include "PhoneListener.h"

FirebaseLib::FirebaseManager::FirebaseManager(const std::string& email, const std::string& password) :
    m_Auth(nullptr),
    m_App(nullptr),
    m_Email(email),
    m_Password(password)
{
    SetupApp();
    SetupAuth();
    SetupDatabase();
    SetupCloudStorage();

    SetEmail(email.c_str());
    SetPassword(password.c_str());
    Login();
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
    SetupCloudStorage();
}

FirebaseLib::FirebaseManager::~FirebaseManager()
{
    Destroy();
}

void FirebaseLib::FirebaseManager::Destroy()
{
    delete m_Storage;
    m_Storage = nullptr;

    delete m_Database;
    m_Database = nullptr;

    delete m_Auth;
    m_Auth = nullptr;

    delete m_App;
    m_App = nullptr;
}

bool FirebaseLib::FirebaseManager::Register()
{
    switch (GetAccountType())
    {
    // Anonymous
    case 1:
        DeleteCurrentAccount();
        break;
    default:
        SignOut();
    }

    Future<::firebase::auth::User*> sign_in_future =
        m_Auth->CreateUserWithEmailAndPassword(m_Email.c_str(), m_Password.c_str());
    WaitForFuture(sign_in_future, "FirebaseManager::Register()", kAuthErrorNone);
    m_Auth->current_user()->SendEmailVerification();

    const AuthError error = static_cast<AuthError>(sign_in_future.error());
    Future<void> update_future;
    switch (error)
    {
    case kAuthErrorNone:
    {
        LastAuthError = kAuthErrorNone;
        SignedUser = sign_in_future;

        User::UserProfile profile;
        profile.display_name = m_DisplayName.c_str();

        update_future = m_Auth->current_user()->UpdateUserProfile(profile);

        WaitForFuture(update_future, "FirebaseManager::Register()::UpdateProfile", kAuthErrorNone);
        if(static_cast<AuthError>(update_future.error()) != kAuthErrorNone)
        {
            LastErrorString = update_future.error_message();
            LastAuthError = error;
            return false;
        }

        AddAccountToDatabase();
        return true;
    }        
    default:
        LastErrorString = sign_in_future.error_message();
        LastAuthError = error;
        return false;
    }
}

bool FirebaseLib::FirebaseManager::Login()
{
    if (StilSignedIn())
    {
        switch (GetAccountType())
        {
        // Anonymous
        case 1:
            DeleteCurrentAccount();
            break;
        default:
            SignOut();
        }
    }

    //m_Auth->
    Future<User*> sign_in_future =
        m_Auth->SignInWithEmailAndPassword(m_Email.c_str(), m_Password.c_str());

    WaitForFuture(sign_in_future, "FirebaseManager::Login()", kAuthErrorNone);
    
    const AuthError error = static_cast<AuthError>(sign_in_future.error());
    switch (error)
    {
    case kAuthErrorNone:
        LastAuthError = kAuthErrorNone;
        SignedUser = sign_in_future;
        break;
    default:
        LastErrorString = sign_in_future.error_message();
        LastAuthError = error;
        SignOut();
        return false;
    }

    if (!m_Auth->current_user()->is_email_verified())
    {
        LastErrorString = "FirebaseManager::Login() : Not verified account! Please check your email.";
        LastAuthError = firebase::auth::kAuthErrorUnverifiedEmail;
        SignOut();
        return false;
    }

    return true;
}

void FirebaseLib::FirebaseManager::SignOut()
{
    if (StilSignedIn())
    {
        switch (GetAccountType())
        {
            // Anonymous
        case 1:
            DeleteCurrentAccount();
            break;
        case 2:
            firebase::database::DatabaseReference ref = m_Database->GetReference("Users").PushChild();
            // Set value to false as they arn't sign in
            firebase::Future<void> f1 = ref.Child(m_Auth->current_user()->uid()).SetValue(false);
            WaitForCompletion(f1, "FirebaseManager::SignOut()");
            break;
        }
    }

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

std::string& FirebaseLib::FirebaseManager::GetDisplayName()
{
    return m_DisplayName;
}

void FirebaseLib::FirebaseManager::SetEmail(const char* userName)
{
    m_Email = userName;
}

void FirebaseLib::FirebaseManager::SetPassword(const char* password)
{
    m_Password = password;
}

void FirebaseLib::FirebaseManager::SetDisplayName(const char* name)
{
    m_DisplayName = name;
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
    m_Auth->SignInAnonymously();
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

void FirebaseLib::FirebaseManager::SetupPhoneAuthentication(const char* phoneNumber)
{
    PhoneListener listener;
    PhoneAuthProvider& phone_provider = PhoneAuthProvider::GetInstance(m_Auth);
    phone_provider.VerifyPhoneNumber(phoneNumber, 0,
        nullptr, &listener);

    int wait_ms = 0;
    while (listener.NumCallsOnVerificationComplete() == 0 &&
        listener.NumCallsOnVerificationFailed() == 0 &&
        listener.NumCallsOnCodeSent() == 0)
    {
        if (wait_ms > PhoneAuthCodeSendWaitMs)
        {
            break;
        }
        ProcessEvents(PhoneWaitIntervalMs);
        wait_ms += PhoneWaitIntervalMs;
        LogMessage(".");
    }
    if (wait_ms > PhoneAuthCodeSendWaitMs ||
        listener.NumCallsOnVerificationFailed())
    {
        LogMessage("FirebaseManager::SetupPhoneAuthentication() : ERROR: SMS with verification code not sent.");
        LastAuthError = kAuthErrorFailure;
        LastErrorString = "FirebaseManager::SetupPhoneAuthentication() : SMS with verification code not sent.";
    }
    else
    {
        LogMessage("FirebaseManager::SetupPhoneAuthentication() : SMS verification code sent.");
    }
    // TODO:
    // We must now wait and get the code from the user's phone. Because we are implementing the
    // graphical forms on .net c#, we have to cut this part short. Once the user provides the code
    // needed to continue, the Form/Manager will go onto the second part in SetupPhoneAuthenticationPartTwo()
    // 
    // However, we are still time sensitive to ensure the code is still valid and not stale. The problem here
    // is that we would need to process events somewhere. The first idea I have is to create a second thread
    // that Processes Events similar as to what was done above. The thread would process and kill itself
    // should the waited millaseconds reached max wait time, or if the retrieval was successful according to 
    // the phone listener. This would also mean the listener is not a local variable within the function and is
    // instead created/stored as a class instance within the manager.
    //
    // The other option is to continue in this function with another process events while-loop. However that 
    // would then require us to do some shenanigans in telling the c# to open the new form to then retrieve the
    // code. This option is not likely viable due to the dll to exe talk and that the application proccess would
    // be hung up on the process events while loop.
}

std::string FirebaseLib::FirebaseManager::GetUserId()
{
    return m_Auth->current_user()->uid();
}

std::string FirebaseLib::FirebaseManager::GetLauncherVersion()
{
    std::string version = "";
    firebase::database::DatabaseReference ref = m_Database->GetReference("ClientData");

    firebase::Future<firebase::database::DataSnapshot> f1 =
        ref.Child("Version").GetValue();

    WaitForCompletion(f1, "FirebaseManager::GetLauncherVersion()");

    return f1.result()->value().string_value();
}

void FirebaseLib::FirebaseManager::DownloadLauncher()
{
    const size_t BufferSize = 14000;
    char buffer[BufferSize];

    TCHAR DirBuffer[MAX_PATH] = { 0 };
    GetModuleFileName(NULL, DirBuffer, MAX_PATH);    
    
    using convert_type = std::codecvt_utf8<wchar_t>;
    std::wstring_convert<convert_type, wchar_t> converter;
    
    std::string directory = converter.to_bytes(std::wstring(DirBuffer));
    std::string::size_type pos = directory.find_last_of("\\/");
    directory = directory.substr(0, pos);
    
    directory += "\\NewFiles";
    
    StorageReference fileRef = m_CloudRootReference.Child("SWGLauncher.zip");
    Future<size_t> fileFuture = fileRef.GetFile(".\\SWGLauncher.zip", nullptr, nullptr);
    WaitForCompletion(fileFuture, "FirebaseManager::DownloadLauncher()");
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

    void* initialize_targets[] = { &m_Auth, &m_Database, &m_Storage };

    const firebase::ModuleInitializer::InitializerFn initializers[] = {
      [](::firebase::App* app, void* data) {
        LogMessage("FirebaseManager::SetupApp() : Attempt to initialize Firebase Auth.");
        void** targets = reinterpret_cast<void**>(data);
        ::firebase::InitResult result;
        *reinterpret_cast<::firebase::auth::Auth**>(targets[0]) =
            ::firebase::auth::Auth::GetAuth(app, &result);
        return result;
      },
      [](::firebase::App* app, void* data) {
        LogMessage("FirebaseManager::SetupApp() : Attempt to initialize Firebase Database.");
        void** targets = reinterpret_cast<void**>(data);
        ::firebase::InitResult result;
        *reinterpret_cast<::firebase::database::Database**>(targets[1]) =
            ::firebase::database::Database::GetInstance(app, &result);
        return result;
      },
      [](::firebase::App* app, void* data) {
            LogMessage("FirebaseManager::SetupApp() : Attempt to initialize Cloud Storage.");
            void** targets = reinterpret_cast<void**>(data);
            ::firebase::InitResult result;
            firebase::storage::Storage* storage =
                firebase::storage::Storage::GetInstance(app, kStorageUrl, &result);
            *reinterpret_cast<::firebase::storage::Storage**>(targets[2]) = storage;
            LogMessage("FirebaseManager::SetupApp() : Initialized storage with URL %s, %s",
                       kStorageUrl ? kStorageUrl : "(null)",
                       storage->url().c_str());
            return result;
        } };

    ::firebase::ModuleInitializer initializer;
    initializer.Initialize(m_App, initialize_targets, initializers,
        sizeof(initializers) / sizeof(initializers[0]));

    WaitForCompletion(initializer.InitializeLastResult(), "FirebaseManager::SetupApp()");
}

void FirebaseLib::FirebaseManager::SetupAuth()
{
    m_Auth = Auth::GetAuth(m_App);
}

void FirebaseLib::FirebaseManager::SendPasswordReset(const char* emailAddress)
{
    Future<void> future = m_Auth->SendPasswordResetEmail(emailAddress);
    WaitForCompletion(future,"FirebaseManager::SendPasswordReset()");

    const AuthError error = static_cast<AuthError>(future.error());
    switch (error)
    {
    case kAuthErrorNone:
        LastAuthError = kAuthErrorNone;
        break;
    default:
        LastErrorString = future.error_message();
        LastAuthError = error;
    }
}

void FirebaseLib::FirebaseManager::SetupDatabase()
{
    m_SavedUrl = m_Database->url();
    m_Database->set_persistence_enabled(false);
}

void FirebaseLib::FirebaseManager::AddAccountToDatabase()
{
    firebase::database::DatabaseReference ref = m_Database->GetReference("Users").PushChild();
}

bool FirebaseLib::FirebaseManager::IsAccountAlreadyLoggedIn()
{
    firebase::database::DatabaseReference ref = m_Database->GetReference("Users").PushChild();
    // Set value to false as they arn't sign in
    firebase::Future<firebase::database::DataSnapshot> f1 = ref.Child(m_Auth->current_user()->uid()).GetValue();
    WaitForCompletion(f1, "FirebaseManager::IsAccountAlreadyLoggedIn()");
    return f1.result();
}

void FirebaseLib::FirebaseManager::SetupCloudStorage()
{
    m_CloudRootReference = m_Storage->GetReference(RootString);
}
