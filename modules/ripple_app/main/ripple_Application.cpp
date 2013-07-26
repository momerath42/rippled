//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

// VFALCO TODO Clean this global up
volatile bool doShutdown = false;

class Application;

SETUP_LOG (Application)

// VFALCO TODO Move the function definitions into the class declaration
class ApplicationImp
    : public Application
    , public SharedSingleton <ApplicationImp>
    , public Validators::Listener
    , public NodeStore::Scheduler
    , LeakChecked <ApplicationImp>
{
public:
    static ApplicationImp* createInstance ()
    {
        return new ApplicationImp;
    }

    ApplicationImp ()
    //
    // VFALCO NOTE Change this to control whether or not the Application
    //             object is destroyed on exit
    //
    #if 1
        // Application object will be deleted on exit. If the code doesn't exit
        // cleanly this could cause hangs or crashes on exit.
        //
        : SharedSingleton <ApplicationImp> (SingletonLifetime::persistAfterCreation)
    #else
        // This will make it so that the Application object is not deleted on exit.
        //
        : SharedSingleton <Application> (SingletonLifetime::neverDestroyed)
    #endif
        , mIOService ((getConfig ().NODE_SIZE >= 2) ? 2 : 1)
        , mIOWork (mIOService)
        , mNetOps (new NetworkOPs (&mLedgerMaster))
        , m_rpcServerHandler (*mNetOps)
        , mTempNodeCache ("NodeCache", 16384, 90)
        , mSLECache ("LedgerEntryCache", 4096, 120)
        , mSNTPClient (mAuxService)
        , mJobQueue (mIOService)
        // VFALCO New stuff
        , m_nodeStore (NodeStore::New (
            getConfig ().nodeDatabase,
            getConfig ().ephemeralNodeDatabase,
            *this))
        , m_validators (Validators::New (this))
        , mFeatures (IFeatures::New (2 * 7 * 24 * 60 * 60, 200)) // two weeks, 200/256
        , mFeeVote (IFeeVote::New (10, 50 * SYSTEM_CURRENCY_PARTS, 12.5 * SYSTEM_CURRENCY_PARTS))
        , mFeeTrack (ILoadFeeTrack::New ())
        , mHashRouter (IHashRouter::New (IHashRouter::getDefaultHoldTime ()))
        , mValidations (IValidations::New ())
        , mUNL (UniqueNodeList::New ())
        , mProofOfWorkFactory (IProofOfWorkFactory::New ())
        , mPeers (IPeers::New (mIOService))
        , m_loadManager (ILoadManager::New ())
        // VFALCO End new stuff
        // VFALCO TODO replace all NULL with nullptr
        , mRpcDB (NULL)
        , mTxnDB (NULL)
        , mLedgerDB (NULL)
        , mWalletDB (NULL) // VFALCO NOTE are all these 'NULL' ctor params necessary?
        , mPeerDoor (NULL)
        , mRPCDoor (NULL)
        , mWSPublicDoor (NULL)
        , mWSPrivateDoor (NULL)
        , mSweepTimer (mAuxService)
        , mShutdown (false)
    {
        // VFALCO TODO remove these once the call is thread safe.
        HashMaps::getInstance ().initializeNonce <size_t> ();
    }

    ~ApplicationImp ()
    {
        mNetOps = nullptr;

        // VFALCO TODO Wrap these in ScopedPointer
        delete mTxnDB;
        delete mLedgerDB;
        delete mWalletDB;
    }

    //--------------------------------------------------------------------------

    static void callScheduledTask (NodeStore::Scheduler::Task* task, Job&)
    {
        task->performScheduledTask ();
    }

    void scheduleTask (NodeStore::Scheduler::Task* task)
    {
        getJobQueue ().addJob (
            jtWRITE,
            "NodeObject::store",
            BIND_TYPE (&ApplicationImp::callScheduledTask, task, P_1));
    }

    //--------------------------------------------------------------------------

    LocalCredentials& getLocalCredentials ()
    {
        return m_localCredentials ;
    }

    NetworkOPs& getOPs ()
    {
        return *mNetOps;
    }

    boost::asio::io_service& getIOService ()
    {
        return mIOService;
    }

    LedgerMaster& getLedgerMaster ()
    {
        return mLedgerMaster;
    }

    InboundLedgers& getInboundLedgers ()
    {
        return m_inboundLedgers;
    }

    TransactionMaster& getMasterTransaction ()
    {
        return mMasterTransaction;
    }

    NodeCache& getTempNodeCache ()
    {
        return mTempNodeCache;
    }

    NodeStore& getNodeStore ()
    {
        return *m_nodeStore;
    }

    JobQueue& getJobQueue ()
    {
        return mJobQueue;
    }

    MasterLockType& getMasterLock ()
    {
        return mMasterLock;
    }

    ILoadManager& getLoadManager ()
    {
        return *m_loadManager;
    }

    TXQueue& getTxnQueue ()
    {
        return mTxnQueue;
    }

    PeerDoor& getPeerDoor ()
    {
        return *mPeerDoor;
    }

    OrderBookDB& getOrderBookDB ()
    {
        return mOrderBookDB;
    }

    SLECache& getSLECache ()
    {
        return mSLECache;
    }

    Validators& getValidators ()
    {
        return *m_validators;
    }

    IFeatures& getFeatureTable ()
    {
        return *mFeatures;
    }

    ILoadFeeTrack& getFeeTrack ()
    {
        return *mFeeTrack;
    }

    IFeeVote& getFeeVote ()
    {
        return *mFeeVote;
    }

    IHashRouter& getHashRouter ()
    {
        return *mHashRouter;
    }

    IValidations& getValidations ()
    {
        return *mValidations;
    }

    UniqueNodeList& getUNL ()
    {
        return *mUNL;
    }

    IProofOfWorkFactory& getProofOfWorkFactory ()
    {
        return *mProofOfWorkFactory;
    }

    IPeers& getPeers ()
    {
        return *mPeers;
    }

    // VFALCO TODO Move these to the .cpp
    bool running ()
    {
        return mTxnDB != NULL;    // VFALCO TODO replace with nullptr when beast is available
    }
    bool getSystemTimeOffset (int& offset)
    {
        return mSNTPClient.getOffset (offset);
    }

    DatabaseCon* getRpcDB ()
    {
        return mRpcDB;
    }
    DatabaseCon* getTxnDB ()
    {
        return mTxnDB;
    }
    DatabaseCon* getLedgerDB ()
    {
        return mLedgerDB;
    }
    DatabaseCon* getWalletDB ()
    {
        return mWalletDB;
    }

    bool isShutdown ()
    {
        return mShutdown;
    }
    void setup ();
    void run ();
    void stop ();
    void sweep ();
    void doSweep (Job&);

private:
    void updateTables ();
    void startNewLedger ();
    bool loadOldLedger (const std::string&, bool);

private:
    boost::asio::io_service mIOService;
    boost::asio::io_service mAuxService;
    // The lifetime of the io_service::work object informs the io_service
    // of when the work starts and finishes. io_service::run() will not exit
    // while the work object exists.
    //
    boost::asio::io_service::work mIOWork;

    MasterLockType mMasterLock;

    LocalCredentials   m_localCredentials;
    LedgerMaster       mLedgerMaster;
    InboundLedgers     m_inboundLedgers;
    TransactionMaster  mMasterTransaction;
    ScopedPointer <NetworkOPs> mNetOps;
    RPCServerHandler   m_rpcServerHandler;
    NodeCache          mTempNodeCache;
    SLECache           mSLECache;
    SNTPClient         mSNTPClient;
    JobQueue           mJobQueue;
    TXQueue            mTxnQueue;
    OrderBookDB        mOrderBookDB;

    // VFALCO Clean stuff
    ScopedPointer <NodeStore> m_nodeStore;
    ScopedPointer <Validators> m_validators;
    ScopedPointer <IFeatures> mFeatures;
    ScopedPointer <IFeeVote> mFeeVote;
    ScopedPointer <ILoadFeeTrack> mFeeTrack;
    ScopedPointer <IHashRouter> mHashRouter;
    ScopedPointer <IValidations> mValidations;
    ScopedPointer <UniqueNodeList> mUNL;
    ScopedPointer <IProofOfWorkFactory> mProofOfWorkFactory;
    ScopedPointer <IPeers> mPeers;
    ScopedPointer <ILoadManager> m_loadManager;
    // VFALCO End Clean stuff

    DatabaseCon* mRpcDB;
    DatabaseCon* mTxnDB;
    DatabaseCon* mLedgerDB;
    DatabaseCon* mWalletDB;

    ScopedPointer <PeerDoor> mPeerDoor;
    ScopedPointer <RPCDoor>  mRPCDoor;
    ScopedPointer <WSDoor>   mWSPublicDoor;
    ScopedPointer <WSDoor>   mWSPrivateDoor;

    boost::asio::deadline_timer mSweepTimer;

    bool volatile mShutdown;
};

// VFALCO TODO Why do we even have this function?
//             It could just be handled in the destructor.
//
void ApplicationImp::stop ()
{
    WriteLog (lsINFO, Application) << "Received shutdown request";
    StopSustain ();
    mShutdown = true;
    mIOService.stop ();
    m_nodeStore = nullptr;
    mValidations->flush ();
    mAuxService.stop ();
    mJobQueue.shutdown ();

    WriteLog (lsINFO, Application) << "Stopped: " << mIOService.stopped ();
    mShutdown = false;
}

static void InitDB (DatabaseCon** dbCon, const char* fileName, const char* dbInit[], int dbCount)
{
    *dbCon = new DatabaseCon (fileName, dbInit, dbCount);
}

#ifdef SIGINT
void sigIntHandler (int)
{
    doShutdown = true;
}
#endif

// VFALCO TODO Figure this out it looks like the wrong tool
static void runAux (boost::asio::io_service& svc)
{
    setCallingThreadName ("aux");
    svc.run ();
}

static void runIO (boost::asio::io_service& io)
{
    setCallingThreadName ("io");
    io.run ();
}

// VFALCO TODO Break this function up into many small initialization segments.
//             Or better yet refactor these initializations into RAII classes
//             which are members of the Application object.
//
void ApplicationImp::setup ()
{
    // VFALCO NOTE: 0 means use heuristics to determine the thread count.
    mJobQueue.setThreadCount (0, getConfig ().RUN_STANDALONE);

    mSweepTimer.expires_from_now (boost::posix_time::seconds (10));
    mSweepTimer.async_wait (BIND_TYPE (&ApplicationImp::sweep, this));

    m_loadManager->startThread ();

#if ! BEAST_WIN32
#ifdef SIGINT

    if (!getConfig ().RUN_STANDALONE)
    {
        struct sigaction sa;
        memset (&sa, 0, sizeof (sa));
        sa.sa_handler = sigIntHandler;
        sigaction (SIGINT, &sa, NULL);
    }

#endif
#endif

    assert (mTxnDB == NULL);

    if (!getConfig ().DEBUG_LOGFILE.empty ())
    {
        // Let debug messages go to the file but only WARNING or higher to regular output (unless verbose)
        Log::setLogFile (getConfig ().DEBUG_LOGFILE);

        if (Log::getMinSeverity () > lsDEBUG)
            LogPartition::setSeverity (lsDEBUG);
    }

    boost::thread (BIND_TYPE (runAux, boost::ref (mAuxService))).detach ();

    if (!getConfig ().RUN_STANDALONE)
        mSNTPClient.init (getConfig ().SNTP_SERVERS);

    //
    // Construct databases.
    //
    boost::thread t1 (BIND_TYPE (&InitDB, &mRpcDB, "rpc.db", RpcDBInit, RpcDBCount));
    boost::thread t2 (BIND_TYPE (&InitDB, &mTxnDB, "transaction.db", TxnDBInit, TxnDBCount));
    boost::thread t3 (BIND_TYPE (&InitDB, &mLedgerDB, "ledger.db", LedgerDBInit, LedgerDBCount));
    boost::thread t4 (BIND_TYPE (&InitDB, &mWalletDB, "wallet.db", WalletDBInit, WalletDBCount));
    t1.join ();
    t2.join ();
    t3.join ();
    t4.join ();

    leveldb::Options options;
    options.create_if_missing = true;
    options.block_cache = leveldb::NewLRUCache (getConfig ().getSize (siHashNodeDBCache) * 1024 * 1024);

    getApp().getLedgerDB ()->getDB ()->executeSQL (boost::str (boost::format ("PRAGMA cache_size=-%d;") %
            (getConfig ().getSize (siLgrDBCache) * 1024)));
    getApp().getTxnDB ()->getDB ()->executeSQL (boost::str (boost::format ("PRAGMA cache_size=-%d;") %
            (getConfig ().getSize (siTxnDBCache) * 1024)));

    mTxnDB->getDB ()->setupCheckpointing (&mJobQueue);
    mLedgerDB->getDB ()->setupCheckpointing (&mJobQueue);

    if (!getConfig ().RUN_STANDALONE)
        updateTables ();

    mFeatures->addInitialFeatures ();

    if (getConfig ().START_UP == Config::FRESH)
    {
        WriteLog (lsINFO, Application) << "Starting new Ledger";

        startNewLedger ();
    }
    else if ((getConfig ().START_UP == Config::LOAD) || (getConfig ().START_UP == Config::REPLAY))
    {
        WriteLog (lsINFO, Application) << "Loading specified Ledger";

        if (!loadOldLedger (getConfig ().START_LEDGER, getConfig ().START_UP == Config::REPLAY))
        {
            getApp().stop ();
            exit (-1);
        }
    }
    else if (getConfig ().START_UP == Config::NETWORK)
    {
        // This should probably become the default once we have a stable network
        if (!getConfig ().RUN_STANDALONE)
            mNetOps->needNetworkLedger ();

        startNewLedger ();
    }
    else
        startNewLedger ();

    mOrderBookDB.setup (getApp().getLedgerMaster ().getCurrentLedger ());

    //
    // Begin validation and ip maintenance.
    // - LocalCredentials maintains local information: including identity and network connection persistence information.
    //
    m_localCredentials.start ();

    //
    // Set up UNL.
    //
    if (!getConfig ().RUN_STANDALONE)
        getUNL ().nodeBootstrap ();

    mValidations->tune (getConfig ().getSize (siValidationsSize), getConfig ().getSize (siValidationsAge));
    m_nodeStore->tune (getConfig ().getSize (siNodeCacheSize), getConfig ().getSize (siNodeCacheAge));
    mLedgerMaster.tune (getConfig ().getSize (siLedgerSize), getConfig ().getSize (siLedgerAge));
    mSLECache.setTargetSize (getConfig ().getSize (siSLECacheSize));
    mSLECache.setTargetAge (getConfig ().getSize (siSLECacheAge));

    mLedgerMaster.setMinValidations (getConfig ().VALIDATION_QUORUM);

    //
    // Allow peer connections.
    //
    if (!getConfig ().RUN_STANDALONE)
    {
        try
        {
            mPeerDoor = PeerDoor::New (
                getConfig ().PEER_IP,
                getConfig ().PEER_PORT,
                getConfig ().PEER_SSL_CIPHER_LIST,
                mIOService);
        }
        catch (const std::exception& e)
        {
            // Must run as directed or exit.
            WriteLog (lsFATAL, Application) << boost::str (boost::format ("Can not open peer service: %s") % e.what ());

            exit (3);
        }
    }
    else
    {
        WriteLog (lsINFO, Application) << "Peer interface: disabled";
    }

    //
    // Allow RPC connections.
    //
    if (! getConfig ().getRpcIP().empty () && getConfig ().getRpcPort() != 0)
    {
        try
        {
            mRPCDoor = new RPCDoor (mIOService, m_rpcServerHandler);
        }
        catch (const std::exception& e)
        {
            // Must run as directed or exit.
            WriteLog (lsFATAL, Application) << boost::str (boost::format ("Can not open RPC service: %s") % e.what ());

            exit (3);
        }
    }
    else
    {
        WriteLog (lsINFO, Application) << "RPC interface: disabled";
    }

    //
    // Allow private WS connections.
    //
    if (!getConfig ().WEBSOCKET_IP.empty () && getConfig ().WEBSOCKET_PORT)
    {
        try
        {
            mWSPrivateDoor  = WSDoor::createWSDoor (getConfig ().WEBSOCKET_IP, getConfig ().WEBSOCKET_PORT, false);
        }
        catch (const std::exception& e)
        {
            // Must run as directed or exit.
            WriteLog (lsFATAL, Application) << boost::str (boost::format ("Can not open private websocket service: %s") % e.what ());

            exit (3);
        }
    }
    else
    {
        WriteLog (lsINFO, Application) << "WS private interface: disabled";
    }

    //
    // Allow public WS connections.
    //
    if (!getConfig ().WEBSOCKET_PUBLIC_IP.empty () && getConfig ().WEBSOCKET_PUBLIC_PORT)
    {
        try
        {
            mWSPublicDoor   = WSDoor::createWSDoor (getConfig ().WEBSOCKET_PUBLIC_IP, getConfig ().WEBSOCKET_PUBLIC_PORT, true);
        }
        catch (const std::exception& e)
        {
            // Must run as directed or exit.
            WriteLog (lsFATAL, Application) << boost::str (boost::format ("Can not open public websocket service: %s") % e.what ());

            exit (3);
        }
    }
    else
    {
        WriteLog (lsINFO, Application) << "WS public interface: disabled";
    }

    //
    // Begin connecting to network.
    //
    if (!getConfig ().RUN_STANDALONE)
        mPeers->start ();

    if (getConfig ().RUN_STANDALONE)
    {
        WriteLog (lsWARNING, Application) << "Running in standalone mode";

        mNetOps->setStandAlone ();
    }
    else
    {
        // VFALCO NOTE the state timer resets the deadlock detector.
        //
        mNetOps->setStateTimer ();
    }
}

void ApplicationImp::run ()
{
    if (getConfig ().NODE_SIZE >= 2)
    {
        boost::thread (BIND_TYPE (runIO, boost::ref (mIOService))).detach ();
    }

    if (!getConfig ().RUN_STANDALONE)
    {
        // VFALCO NOTE This seems unnecessary. If we properly refactor the load
        //             manager then the deadlock detector can just always be "armed"
        //
        getApp().getLoadManager ().activateDeadlockDetector ();
    }

    mIOService.run (); // This blocks

    if (mWSPublicDoor)
        mWSPublicDoor->stop ();

    if (mWSPrivateDoor)
        mWSPrivateDoor->stop ();

    // VFALCO TODO Try to not have to do this early, by using observers to
    //             eliminate LoadManager's dependency inversions.
    //
    // This deletes the object and therefore, stops the thread.
    m_loadManager = nullptr;

    mSweepTimer.cancel();

    WriteLog (lsINFO, Application) << "Done.";

    // VFALCO NOTE This is a sign that something is wrong somewhere, it
    //             shouldn't be necessary to sleep until some flag is set.
    while (mShutdown)
        boost::this_thread::sleep (boost::posix_time::milliseconds (100));
}

void ApplicationImp::sweep ()
{
    boost::filesystem::space_info space = boost::filesystem::space (getConfig ().DATA_DIR);

    // VFALCO TODO Give this magic constant a name and move it into a well documented header
    //
    if (space.available < (512 * 1024 * 1024))
    {
        WriteLog (lsFATAL, Application) << "Remaining free disk space is less than 512MB";
        getApp().stop ();
    }

    mJobQueue.addJob(jtSWEEP, "sweep",
        BIND_TYPE(&ApplicationImp::doSweep, this, P_1));
}

void ApplicationImp::doSweep(Job& j)
{
    // VFALCO NOTE Does the order of calls matter?
    // VFALCO TODO fix the dependency inversion using an observer,
    //         have listeners register for "onSweep ()" notification.
    //

    mMasterTransaction.sweep ();
    m_nodeStore->sweep ();
    mLedgerMaster.sweep ();
    mTempNodeCache.sweep ();
    mValidations->sweep ();
    getInboundLedgers ().sweep ();
    mSLECache.sweep ();
    AcceptedLedger::sweep (); // VFALCO NOTE AcceptedLedger is/has a singleton?
    SHAMap::sweep (); // VFALCO NOTE SHAMap is/has a singleton?
    mNetOps->sweepFetchPack ();
    // VFALCO NOTE does the call to sweep() happen on another thread?
    mSweepTimer.expires_from_now (boost::posix_time::seconds (getConfig ().getSize (siSweepInterval)));
    mSweepTimer.async_wait (BIND_TYPE (&ApplicationImp::sweep, this));
}

void ApplicationImp::startNewLedger ()
{
    // New stuff.
    RippleAddress   rootSeedMaster      = RippleAddress::createSeedGeneric ("masterpassphrase");
    RippleAddress   rootGeneratorMaster = RippleAddress::createGeneratorPublic (rootSeedMaster);
    RippleAddress   rootAddress         = RippleAddress::createAccountPublic (rootGeneratorMaster, 0);

    // Print enough information to be able to claim root account.
    WriteLog (lsINFO, Application) << "Root master seed: " << rootSeedMaster.humanSeed ();
    WriteLog (lsINFO, Application) << "Root account: " << rootAddress.humanAccountID ();

    {
        Ledger::pointer firstLedger = boost::make_shared<Ledger> (rootAddress, SYSTEM_CURRENCY_START);
        assert (!!firstLedger->getAccountState (rootAddress));
        // WRITEME: Add any default features
        // WRITEME: Set default fee/reserve
        firstLedger->updateHash ();
        firstLedger->setClosed ();
        firstLedger->setAccepted ();
        mLedgerMaster.pushLedger (firstLedger);

        Ledger::pointer secondLedger = boost::make_shared<Ledger> (true, boost::ref (*firstLedger));
        secondLedger->setClosed ();
        secondLedger->setAccepted ();
        mLedgerMaster.pushLedger (secondLedger, boost::make_shared<Ledger> (true, boost::ref (*secondLedger)), false);
        assert (!!secondLedger->getAccountState (rootAddress));
        mNetOps->setLastCloseTime (secondLedger->getCloseTimeNC ());
    }
}

bool ApplicationImp::loadOldLedger (const std::string& l, bool bReplay)
{
    try
    {
        Ledger::pointer loadLedger, replayLedger;

        if (l.empty () || (l == "latest"))
            loadLedger = Ledger::getLastFullLedger ();
        else if (l.length () == 64)
        {
            // by hash
            uint256 hash;
            hash.SetHex (l);
            loadLedger = Ledger::loadByHash (hash);
        }
        else // assume by sequence
            loadLedger = Ledger::loadByIndex (boost::lexical_cast<uint32> (l));

        if (!loadLedger)
        {
            WriteLog (lsFATAL, Application) << "No Ledger found?" << std::endl;
            return false;
        }

        if (bReplay)
        { // Replay a ledger close with same prior ledger and transactions
            replayLedger = loadLedger; // this ledger holds the transactions we want to replay
            loadLedger = Ledger::loadByIndex (replayLedger->getLedgerSeq() - 1); // this is the prior ledger
            if (!loadLedger || (replayLedger->getParentHash() != loadLedger->getHash()))
            {
                WriteLog (lsFATAL, Application) << "Replay ledger missing/damaged";
                assert (false);
                return false;
            }
        }

        loadLedger->setClosed ();

        WriteLog (lsINFO, Application) << "Loading ledger " << loadLedger->getHash () << " seq:" << loadLedger->getLedgerSeq ();

        if (loadLedger->getAccountHash ().isZero ())
        {
            WriteLog (lsFATAL, Application) << "Ledger is empty.";
            assert (false);
            return false;
        }

        if (!loadLedger->walkLedger ())
        {
            WriteLog (lsFATAL, Application) << "Ledger is missing nodes.";
            return false;
        }

        if (!loadLedger->assertSane ())
        {
            WriteLog (lsFATAL, Application) << "Ledger is not sane.";
            return false;
        }

        mLedgerMaster.setLedgerRangePresent (loadLedger->getLedgerSeq (), loadLedger->getLedgerSeq ());

        Ledger::pointer openLedger = boost::make_shared<Ledger> (false, boost::ref (*loadLedger));
        mLedgerMaster.switchLedgers (loadLedger, openLedger);
        mLedgerMaster.forceValid(loadLedger);
        mNetOps->setLastCloseTime (loadLedger->getCloseTimeNC ());

        if (bReplay)
        { // inject transaction from replayLedger into consensus set
            SHAMap::ref txns = replayLedger->peekTransactionMap();
            Ledger::ref cur = getLedgerMaster().getCurrentLedger();

            for (SHAMapItem::pointer it = txns->peekFirstItem(); it != nullptr; it = txns->peekNextItem(it->getTag()))
            {
                Transaction::pointer txn = replayLedger->getTransaction(it->getTag());
                WriteLog (lsINFO, Application) << txn->getJson(0);
                Serializer s;
                txn->getSTransaction()->add(s);
                if (!cur->addTransaction(it->getTag(), s))
                {
                    WriteLog (lsWARNING, Application) << "Unable to add transaction " << it->getTag();
                }
            }
        }
    }
    catch (SHAMapMissingNode&)
    {
        WriteLog (lsFATAL, Application) << "Data is missing for selected ledger";
        return false;
    }
    catch (boost::bad_lexical_cast&)
    {
        WriteLog (lsFATAL, Application) << "Ledger specified '" << l << "' is not valid";
        return false;
    }

    return true;
}

bool serverOkay (std::string& reason)
{
    if (!getConfig ().ELB_SUPPORT)
        return true;

    if (getApp().isShutdown ())
    {
        reason = "Server is shutting down";
        return false;
    }

    if (getApp().getOPs ().isNeedNetworkLedger ())
    {
        reason = "Not synchronized with network yet";
        return false;
    }

    if (getApp().getOPs ().getOperatingMode () < NetworkOPs::omSYNCING)
    {
        reason = "Not synchronized with network";
        return false;
    }

    if (!getApp().getLedgerMaster().isCaughtUp(reason))
        return false;

    if (getApp().getFeeTrack ().isLoadedLocal ())
    {
        reason = "Too much load";
        return false;
    }

    if (getApp().getOPs ().isFeatureBlocked ())
    {
        reason = "Server version too old";
        return false;
    }

    return true;
}

//VFALCO TODO clean this up since it is just a file holding a single member function definition

static std::vector<std::string> getSchema (DatabaseCon* dbc, const std::string& dbName)
{
    std::vector<std::string> schema;

    std::string sql = "SELECT sql FROM sqlite_master WHERE tbl_name='";
    sql += dbName;
    sql += "';";

    SQL_FOREACH (dbc->getDB (), sql)
    {
        dbc->getDB ()->getStr ("sql", sql);
        schema.push_back (sql);
    }

    return schema;
}

static bool schemaHas (DatabaseCon* dbc, const std::string& dbName, int line, const std::string& content)
{
    std::vector<std::string> schema = getSchema (dbc, dbName);

    if (static_cast<int> (schema.size ()) <= line)
    {
        Log (lsFATAL) << "Schema for " << dbName << " has too few lines";
        throw std::runtime_error ("bad schema");
    }

    return schema[line].find (content) != std::string::npos;
}

static void addTxnSeqField ()
{
    if (schemaHas (getApp().getTxnDB (), "AccountTransactions", 0, "TxnSeq"))
        return;

    Log (lsWARNING) << "Transaction sequence field is missing";

    Database* db = getApp().getTxnDB ()->getDB ();

    std::vector< std::pair<uint256, int> > txIDs;
    txIDs.reserve (300000);

    Log (lsINFO) << "Parsing transactions";
    int i = 0;
    uint256 transID;
    SQL_FOREACH (db, "SELECT TransID,TxnMeta FROM Transactions;")
    {
        Blob rawMeta;
        int metaSize = 2048;
        rawMeta.resize (metaSize);
        metaSize = db->getBinary ("TxnMeta", &*rawMeta.begin (), rawMeta.size ());

        if (metaSize > static_cast<int> (rawMeta.size ()))
        {
            rawMeta.resize (metaSize);
            db->getBinary ("TxnMeta", &*rawMeta.begin (), rawMeta.size ());
        }
        else rawMeta.resize (metaSize);

        std::string tid;
        db->getStr ("TransID", tid);
        transID.SetHex (tid, true);

        if (rawMeta.size () == 0)
        {
            txIDs.push_back (std::make_pair (transID, -1));
            Log (lsINFO) << "No metadata for " << transID;
        }
        else
        {
            TransactionMetaSet m (transID, 0, rawMeta);
            txIDs.push_back (std::make_pair (transID, m.getIndex ()));
        }

        if ((++i % 1000) == 0)
            Log (lsINFO) << i << " transactions read";
    }

    Log (lsINFO) << "All " << i << " transactions read";

    db->executeSQL ("BEGIN TRANSACTION;");

    Log (lsINFO) << "Dropping old index";
    db->executeSQL ("DROP INDEX AcctTxIndex;");

    Log (lsINFO) << "Altering table";
    db->executeSQL ("ALTER TABLE AccountTransactions ADD COLUMN TxnSeq INTEGER;");

    typedef std::pair<uint256, int> u256_int_pair_t;
    boost::format fmt ("UPDATE AccountTransactions SET TxnSeq = %d WHERE TransID = '%s';");
    i = 0;
    BOOST_FOREACH (u256_int_pair_t & t, txIDs)
    {
        db->executeSQL (boost::str (fmt % t.second % t.first.GetHex ()));

        if ((++i % 1000) == 0)
            Log (lsINFO) << i << " transactions updated";
    }

    Log (lsINFO) << "Building new index";
    db->executeSQL ("CREATE INDEX AcctTxIndex ON AccountTransactions(Account, LedgerSeq, TxnSeq, TransID);");
    db->executeSQL ("END TRANSACTION;");
}

void ApplicationImp::updateTables ()
{
    if (getConfig ().nodeDatabase.size () <= 0)
    {
        Log (lsFATAL) << "The [node_db] configuration setting has been updated and must be set";
        StopSustain ();
        exit (1);
    }

    // perform any needed table updates
    assert (schemaHas (getApp().getTxnDB (), "AccountTransactions", 0, "TransID"));
    assert (!schemaHas (getApp().getTxnDB (), "AccountTransactions", 0, "foobar"));
    addTxnSeqField ();

    if (schemaHas (getApp().getTxnDB (), "AccountTransactions", 0, "PRIMARY"))
    {
        Log (lsFATAL) << "AccountTransactions database should not have a primary key";
        StopSustain ();
        exit (1);
    }

    if (getConfig ().importNodeDatabase.size () > 0)
    {
        ScopedPointer <NodeStore> source (NodeStore::New (getConfig ().importNodeDatabase));

        WriteLog (lsWARNING, NodeObject) <<
            "Node import from '" << source->getName () << "' to '"
                                 << getApp().getNodeStore().getName () << "'.";

        getApp().getNodeStore().import (*source);
    }
}

//------------------------------------------------------------------------------

Application& getApp ()
{
    return *ApplicationImp::getInstance ();
}