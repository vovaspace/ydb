#include <library/cpp/testing/unittest/registar.h>
#include <library/cpp/testing/unittest/tests_data.h>
#include <ydb/library/actors/interconnect/interconnect.h>
#include <ydb/library/actors/helpers/selfping_actor.h>
#include <library/cpp/json/json_value.h>
#include <library/cpp/json/json_reader.h>
#include <util/stream/null.h>
#include <ydb/core/viewer/protos/viewer.pb.h>
#include <ydb/core/blobstorage/base/blobstorage_events.h>
#include "json_handlers.h"
#include "json_tabletinfo.h"
#include "json_vdiskinfo.h"
#include "json_pdiskinfo.h"

#include <library/cpp/testing/unittest/registar.h>
#include <library/cpp/testing/unittest/tests_data.h>
#include <ydb/core/testlib/test_client.h>
#include <ydb/core/testlib/tenant_runtime.h>
#include <ydb/public/lib/deprecated/kicli/kicli.h>

#include <ydb/core/node_whiteboard/node_whiteboard.h>
#include <ydb/library/actors/core/interconnect.h>

using namespace NKikimr;
using namespace NViewer;
using namespace NKikimrWhiteboard;

using namespace NSchemeShard;
using namespace Tests;
using namespace NMonitoring;

#ifdef NDEBUG
#define Ctest Cnull
#else
#define Ctest Cerr
#endif

#ifdef address_sanitizer_enabled
#define SANITIZER_TYPE address
#endif
#ifdef memory_sanitizer_enabled
#define SANITIZER_TYPE memory
#endif
#ifdef thread_sanitizer_enabled
#define SANITIZER_TYPE thread
#endif

using duration_nano_t = std::chrono::duration<ui64, std::nano>;
using duration_t = std::chrono::duration<double>;

duration_t GetBasePerformance() {
    duration_nano_t accm{};
    for (int i = 0; i < 1000000; ++i) {
        accm += duration_nano_t(NActors::MeasureTaskDurationNs());
    }
    return std::chrono::duration_cast<duration_t>(accm);
}

double BASE_PERF = GetBasePerformance().count();

Y_UNIT_TEST_SUITE(Viewer) {
    Y_UNIT_TEST(TabletMerging) {
        THPTimer timer;
        Cerr << "BASE_PERF = " << BASE_PERF << Endl;
        {
            TMap<ui32, TString> nodesBlob;
            timer.Reset();
            for (ui32 nodeId = 1; nodeId <= 1000; ++nodeId) {
                NKikimrWhiteboard::TEvTabletStateResponse nodeData;
                nodeData.MutableTabletStateInfo()->Reserve(10000);
                for (ui32 tabletId = 1; tabletId <= 10000; ++tabletId) {
                    NKikimrWhiteboard::TTabletStateInfo* tabletData = nodeData.AddTabletStateInfo();
                    tabletData->SetTabletId(tabletId);
                    tabletData->SetLeader(true);
                    tabletData->SetGeneration(13);
                    tabletData->SetChangeTime(TInstant::Now().MilliSeconds());
                    tabletData->MutableTenantId()->SetSchemeShard(8);
                    tabletData->MutableTenantId()->SetPathId(14);
                    tabletData->MutableChannelGroupIDs()->Add(9);
                    tabletData->MutableChannelGroupIDs()->Add(10);
                    tabletData->MutableChannelGroupIDs()->Add(11);
                }
                nodesBlob[nodeId] = nodeData.SerializeAsString();
            }
            Ctest << "Build = " << timer.Passed() << Endl;
            timer.Reset();
            TMap<ui32, NKikimrWhiteboard::TEvTabletStateResponse> nodesData;
            for (const auto& [nodeId, nodeBlob] : nodesBlob) {
                NKikimrWhiteboard::TEvTabletStateResponse nodeData;
                bool res = nodeData.ParseFromString(nodesBlob[nodeId]);
                Y_UNUSED(res);
                nodesData[nodeId] = std::move(nodeData);
            }
            NKikimrWhiteboard::TEvTabletStateResponse result;
            MergeWhiteboardResponses(result, nodesData);
            Ctest << "Merge = " << timer.Passed() << Endl;
            UNIT_ASSERT_LT(timer.Passed(), 8 * BASE_PERF);
            UNIT_ASSERT_VALUES_EQUAL(result.TabletStateInfoSize(), 10000);
            timer.Reset();
        }
        Ctest << "Destroy = " << timer.Passed() << Endl;
    }

    Y_UNIT_TEST(TabletMergingPacked) {
        THPTimer timer;
        {
            TMap<ui32, TString> nodesBlob;
            timer.Reset();
            for (ui32 nodeId = 1; nodeId <= 1000; ++nodeId) {
                THolder<TEvWhiteboard::TEvTabletStateResponse> nodeData = MakeHolder<TEvWhiteboard::TEvTabletStateResponse>();
                auto* tabletData = nodeData->AllocatePackedResponse(10000);
                for (ui32 tabletId = 1; tabletId <= 10000; ++tabletId) {
                    tabletData->TabletId = tabletId;
                    tabletData->FollowerId = 0;
                    tabletData->Generation = 13;
                    tabletData->Type = NKikimrTabletBase::TTabletTypes::TxProxy;
                    tabletData->State = NKikimrWhiteboard::TTabletStateInfo::Restored;
                    //tabletData->SetChangeTime(TInstant::Now().MilliSeconds());
                    ++tabletData;
                }
                nodesBlob[nodeId] = nodeData->Record.SerializeAsString();
            }
            Ctest << "Build = " << timer.Passed() << Endl;
            TMap<ui32, NKikimrWhiteboard::TEvTabletStateResponse> nodesData;
            for (const auto& [nodeId, nodeBlob] : nodesBlob) {
                NKikimrWhiteboard::TEvTabletStateResponse nodeData;
                bool res = nodeData.ParseFromString(nodesBlob[nodeId]);
                Y_UNUSED(res);
                nodesData[nodeId] = std::move(nodeData);
            }
            NKikimrWhiteboard::TEvTabletStateResponse result;
            MergeWhiteboardResponses(result, nodesData);
            Ctest << "Merge = " << timer.Passed() << Endl;
            UNIT_ASSERT_LT(timer.Passed(), 2 * BASE_PERF);
            UNIT_ASSERT_VALUES_EQUAL(result.TabletStateInfoSize(), 10000);
            timer.Reset();
        }
        Ctest << "Destroy = " << timer.Passed() << Endl;
    }

    Y_UNIT_TEST(VDiskMerging) {
        TMap<ui32, NKikimrWhiteboard::TEvVDiskStateResponse> nodesData;
        for (ui32 nodeId = 1; nodeId <= 1000; ++nodeId) {
            NKikimrWhiteboard::TEvVDiskStateResponse& nodeData = nodesData[nodeId];
            nodeData.MutableVDiskStateInfo()->Reserve(10);
            for (ui32 vDiskId = 1; vDiskId <= 1000; ++vDiskId) {
                NKikimrWhiteboard::TVDiskStateInfo* vDiskData = nodeData.AddVDiskStateInfo();
                vDiskData->MutableVDiskId()->SetDomain(vDiskId);
                vDiskData->MutableVDiskId()->SetGroupGeneration(vDiskId);
                vDiskData->MutableVDiskId()->SetGroupID(vDiskId);
                vDiskData->MutableVDiskId()->SetRing(vDiskId);
                vDiskData->MutableVDiskId()->SetVDisk(vDiskId);
                vDiskData->SetAllocatedSize(10);
                vDiskData->SetChangeTime(TInstant::Now().MilliSeconds());
            }
        }
        Ctest << "Data has built" << Endl;
        THPTimer timer;
        NKikimrWhiteboard::TEvVDiskStateResponse result;
        MergeWhiteboardResponses(result, nodesData);
        Ctest << "Merge = " << timer.Passed() << Endl;
        UNIT_ASSERT_LT(timer.Passed(), 10 * BASE_PERF);
        UNIT_ASSERT_VALUES_EQUAL(result.VDiskStateInfoSize(), 1000);
        Ctest << "Data has merged" << Endl;
    }

    Y_UNIT_TEST(PDiskMerging) {
        TMap<ui32, NKikimrWhiteboard::TEvPDiskStateResponse> nodesData;
        for (ui32 nodeId = 1; nodeId <= 1000; ++nodeId) {
            NKikimrWhiteboard::TEvPDiskStateResponse& nodeData = nodesData[nodeId];
            nodeData.MutablePDiskStateInfo()->Reserve(10);
            for (ui32 pDiskId = 1; pDiskId <= 100; ++pDiskId) {
                NKikimrWhiteboard::TPDiskStateInfo* pDiskData = nodeData.AddPDiskStateInfo();
                pDiskData->SetPDiskId(pDiskId);
                pDiskData->SetAvailableSize(100);
                pDiskData->SetChangeTime(TInstant::Now().MilliSeconds());
            }
        }
        Ctest << "Data has built" << Endl;
        THPTimer timer;
        NKikimrWhiteboard::TEvPDiskStateResponse result;
        MergeWhiteboardResponses(result, nodesData);
        Ctest << "Merge = " << timer.Passed() << Endl;
        UNIT_ASSERT_LT(timer.Passed(), 10 * BASE_PERF);
        UNIT_ASSERT_VALUES_EQUAL(result.PDiskStateInfoSize(), 100000);
        Ctest << "Data has merged" << Endl;
    }

    template <typename T>
    void TestSwagger() {
        T h;
        h.Init();

        TStringStream json;
        json << "{";
        h.PrintForSwagger(json);
        json << "}";

        NJson::TJsonReaderConfig jsonCfg;
        jsonCfg.DontValidateUtf8 = true;
        jsonCfg.AllowComments = false;

        ValidateJsonThrow(json.Str(), jsonCfg);
    }

    Y_UNIT_TEST(Swagger) {
        TestSwagger<TViewerJsonHandlers>();
        TestSwagger<TVDiskJsonHandlers>();
    }

    struct THttpRequest : NMonitoring::IHttpRequest {
        HTTP_METHOD Method;
        TCgiParameters CgiParameters;
        THttpHeaders HttpHeaders;

        THttpRequest(HTTP_METHOD method)
            : Method(method)
        {}

        ~THttpRequest() {}

        const char* GetURI() const override {
            return "";
        }

        const char* GetPath() const override {
            return "";
        }

        const TCgiParameters& GetParams() const override {
            return CgiParameters;
        }

        const TCgiParameters& GetPostParams() const override {
            return CgiParameters;
        }

        TStringBuf GetPostContent() const override {
            return TString();
        }

        HTTP_METHOD GetMethod() const override {
            return Method;
        }

        const THttpHeaders& GetHeaders() const override {
            return HttpHeaders;
        }

        TString GetRemoteAddr() const override {
            return TString();
        }
    };

    class TMonPage: public IMonPage {
    public:
        TMonPage(const TString &path, const TString &title)
            : IMonPage(path, title)
        {
        }

        void Output(IMonHttpRequest&) override {
        }
    };

    void ChangeListNodes(TEvInterconnect::TEvNodesInfo::TPtr* ev, int nodesTotal) {
        auto& nodes = (*ev)->Get()->Nodes;

        auto sample = nodes[0];
        nodes.clear();

        for (int nodeId = 0; nodeId < nodesTotal; nodeId++) {
            nodes.emplace_back(sample);
        }
    }

    void ChangeTabletStateResponse(TEvWhiteboard::TEvTabletStateResponse::TPtr* ev, int tabletsTotal, int& tabletId, int& nodeId) {
        ui64* cookie = const_cast<ui64*>(&(ev->Get()->Cookie));
        *cookie = nodeId;

        auto& record = (*ev)->Get()->Record;
        record.clear_tabletstateinfo();

        for (int i = 0; i < tabletsTotal; i++) {
            auto tablet = record.add_tabletstateinfo();
            tablet->set_tabletid(tabletId++);
            tablet->set_type(NKikimrTabletBase::TTabletTypes::Mediator);
            tablet->set_nodeid(nodeId);
            tablet->set_generation(2);
        }

        nodeId++;
    }

    void ChangeVDiskStateResponse(TEvWhiteboard::TEvVDiskStateResponse::TPtr* ev, NKikimrWhiteboard::EFlag diskSpace, ui64 used, ui64 limit) {
        auto& pbRecord = (*ev)->Get()->Record;
        auto state = pbRecord.add_vdiskstateinfo();
        state->mutable_vdiskid()->set_vdisk(0);
        state->mutable_vdiskid()->set_groupid(0);
        state->mutable_vdiskid()->set_groupgeneration(1);
        state->set_diskspace(diskSpace);
        state->set_vdiskstate(NKikimrWhiteboard::EVDiskState::OK);
        state->set_nodeid(0);
        state->set_allocatedsize(used);
        state->set_availablesize(limit - used);
    }

    void ChangeDescribeSchemeResult(TEvSchemeShard::TEvDescribeSchemeResult::TPtr* ev, int tabletsTotal) {
        auto record = (*ev)->Get()->MutableRecord();
        auto params = record->mutable_pathdescription()->mutable_domaindescription()->mutable_processingparams();

        params->clear_mediators();
        for (int tabletId = 0; tabletId < tabletsTotal; tabletId++) {
            params->add_mediators(tabletId);
        }
    }

    Y_UNIT_TEST(Cluster10000Tablets)
    {
        TPortManager tp;
        ui16 port = tp.GetPort(2134);
        ui16 grpcPort = tp.GetPort(2135);
        auto settings = TServerSettings(port)
                .SetNodeCount(2)
                .SetUseRealThreads(false)
                .SetDomainName("Root")
                .InitKikimrRunConfig();
        TServer server(settings);
        server.EnableGRpc(grpcPort);
        TClient client(settings);
        TTestActorRuntime& runtime = *server.GetRuntime();

        TActorId sender = runtime.AllocateEdgeActor();
        TAutoPtr<IEventHandle> handle;

        THttpRequest httpReq(HTTP_METHOD_GET);
        httpReq.CgiParameters.emplace("tablets", "true");
        auto page = MakeHolder<TMonPage>("viewer", "title");
        TMonService2HttpRequest monReq(nullptr, &httpReq, nullptr, page.Get(), "/json/cluster", nullptr);
        auto request = MakeHolder<NMon::TEvHttpInfo>(monReq);

        int tabletIdCount = 1;
        int nodeIdCount = 1;
        const int nodesTotal = 100;
        const int tabletsTotal = 100;
        auto observerFunc = [&](TAutoPtr<IEventHandle>& ev) {
            Y_UNUSED(ev);
            switch (ev->GetTypeRewrite()) {
                case TEvInterconnect::EvNodesInfo: {
                    auto *x = reinterpret_cast<TEvInterconnect::TEvNodesInfo::TPtr*>(&ev);
                    ChangeListNodes(x, nodesTotal);
                    break;
                }
                case TEvWhiteboard::EvTabletStateResponse: {
                    auto *x = reinterpret_cast<TEvWhiteboard::TEvTabletStateResponse::TPtr*>(&ev);
                    ChangeTabletStateResponse(x, tabletsTotal, tabletIdCount, nodeIdCount);
                    break;
                }
                case NSchemeShard::TEvSchemeShard::EvDescribeSchemeResult: {
                    auto *x = reinterpret_cast<NSchemeShard::TEvSchemeShard::TEvDescribeSchemeResult::TPtr*>(&ev);
                    ChangeDescribeSchemeResult(x, nodesTotal * tabletsTotal);
                    break;
                }
            }

            return TTestActorRuntime::EEventAction::PROCESS;
        };
        runtime.SetObserverFunc(observerFunc);

        THPTimer timer;

        runtime.Send(new IEventHandle(NKikimr::NViewer::MakeViewerID(0), sender, request.Release(), 0));
        runtime.GrabEdgeEvent<NMon::TEvHttpInfoRes>(handle);

        Ctest << "Request timer = " << timer.Passed() << Endl;
        Ctest << "BASE_PERF = " << BASE_PERF << Endl;
        UNIT_ASSERT_LT(timer.Passed(), BASE_PERF);

    }

    Y_UNIT_TEST(TenantInfo5kkTablets)
    {
        const int nodesTotal = 5;
        const int tabletsTotal = 1000000;

        TPortManager tp;
        ui16 port = tp.GetPort(2134);
        ui16 grpcPort = tp.GetPort(2135);
        auto settings = TServerSettings(port)
                .SetNodeCount(nodesTotal)
                .SetUseRealThreads(false)
                .SetDomainName("Root")
                .InitKikimrRunConfig();
        TServer server(settings);
        server.EnableGRpc(grpcPort);

        TClient client(settings);
        TTestActorRuntime& runtime = *server.GetRuntime();

        TActorId sender = runtime.AllocateEdgeActor();
        TAutoPtr<IEventHandle> handle;

        THttpRequest httpReq(HTTP_METHOD_GET);
        httpReq.CgiParameters.emplace("path", "/Root");
        httpReq.CgiParameters.emplace("tablets", "true");
        httpReq.CgiParameters.emplace("storage", "true");
        auto page = MakeHolder<TMonPage>("viewer", "title");
        TMonService2HttpRequest monReq(nullptr, &httpReq, nullptr, page.Get(), "/json/tenantinfo", nullptr);
        auto request = MakeHolder<NMon::TEvHttpInfo>(monReq);

        int tabletIdCount = 1;
        int nodeIdCount = 1;
        auto observerFunc = [&](TAutoPtr<IEventHandle>& ev) {
            Y_UNUSED(ev);
            switch (ev->GetTypeRewrite()) {
                case NConsole::TEvConsole::EvListTenantsResponse: {
                    auto *x = reinterpret_cast<NConsole::TEvConsole::TEvListTenantsResponse::TPtr*>(&ev);
                    Ydb::Cms::ListDatabasesResult listTenantsResult;
                    (*x)->Get()->Record.GetResponse().operation().result().UnpackTo(&listTenantsResult);
                    listTenantsResult.Addpaths("/Root");
                    (*x)->Get()->Record.MutableResponse()->mutable_operation()->mutable_result()->PackFrom(listTenantsResult);
                    break;
                }
                case TEvTxProxySchemeCache::EvNavigateKeySetResult: {
                    auto *x = reinterpret_cast<TEvTxProxySchemeCache::TEvNavigateKeySetResult::TPtr*>(&ev);
                    auto &domain = (*x)->Get()->Request->ResultSet.begin()->DomainInfo;
                    domain->Params.SetHive(1);
                    break;
                }
                case TEvHive::EvResponseHiveDomainStats: {
                    auto *x = reinterpret_cast<TEvHive::TEvResponseHiveDomainStats::TPtr*>(&ev);
                    auto &record = (*x)->Get()->Record;
                    auto *domainStats = record.AddDomainStats();
                    for (int i = 1; i <= nodesTotal; i++) {
                        domainStats->AddNodeIds(i);
                    }
                    domainStats->SetShardId(NKikimr::Tests::SchemeRoot);
                    domainStats->SetPathId(1);
                    break;
                }
                case TEvWhiteboard::EvTabletStateResponse: {
                    auto *x = reinterpret_cast<TEvWhiteboard::TEvTabletStateResponse::TPtr*>(&ev);
                    ChangeTabletStateResponse(x, tabletsTotal, tabletIdCount, nodeIdCount);
                    break;
                }
            }

            return TTestActorRuntime::EEventAction::PROCESS;
        };
        runtime.SetObserverFunc(observerFunc);

        THPTimer timer;

        runtime.Send(new IEventHandle(NKikimr::NViewer::MakeViewerID(0), sender, request.Release(), 0));
        runtime.GrabEdgeEvent<NMon::TEvHttpInfoRes>(handle);

        Ctest << "Request timer = " << timer.Passed() << Endl;
        Ctest << "BASE_PERF = " << BASE_PERF << Endl;

#ifndef SANITIZER_TYPE
#if !defined(NDEBUG) || defined(_hardening_enabled_)
        UNIT_ASSERT_VALUES_EQUAL_C(timer.Passed() < 30 * BASE_PERF, true, "timer = " << timer.Passed() << ", limit = " << 30 * BASE_PERF);
#else
        UNIT_ASSERT_VALUES_EQUAL_C(timer.Passed() < 10 * BASE_PERF, true, "timer = " << timer.Passed() << ", limit = " << 10 * BASE_PERF);
#endif
#endif
    }

    NJson::TJsonValue SendQuery(const TString& query, const TString& schema, const bool base64) {
        TPortManager tp;
        ui16 port = tp.GetPort(2134);
        ui16 grpcPort = tp.GetPort(2135);
        auto settings = TServerSettings(port);
        settings.InitKikimrRunConfig()
                .SetNodeCount(1)
                .SetUseRealThreads(false)
                .SetDomainName("Root");
        TServer server(settings);
        server.EnableGRpc(grpcPort);
        TClient client(settings);
        TTestActorRuntime& runtime = *server.GetRuntime();

        TActorId sender = runtime.AllocateEdgeActor();
        TAutoPtr<IEventHandle> handle;

        THttpRequest httpReq(HTTP_METHOD_GET);
        httpReq.CgiParameters.emplace("schema", schema);
        httpReq.CgiParameters.emplace("base64", base64 ? "true" : "false");
        httpReq.CgiParameters.emplace("query", query);
        auto page = MakeHolder<TMonPage>("viewer", "title");
        TMonService2HttpRequest monReq(nullptr, &httpReq, nullptr, page.Get(), "/json/query", nullptr);
        auto request = MakeHolder<NMon::TEvHttpInfo>(monReq);

        runtime.Send(new IEventHandle(NKikimr::NViewer::MakeViewerID(0), sender, request.Release(), 0));
        NMon::TEvHttpInfoRes* result = runtime.GrabEdgeEvent<NMon::TEvHttpInfoRes>(handle);

        size_t pos = result->Answer.find('{');
        TString jsonResult = result->Answer.substr(pos);
        Ctest << "json result: " << jsonResult << Endl;
        NJson::TJsonValue json;
        try {
            NJson::ReadJsonTree(jsonResult, &json, true);
        }
        catch (yexception ex) {
            Ctest << ex.what() << Endl;
        }
        return json;
    }

    void QueryTest(const TString& query, const bool base64, const TString& reply) {
        NJson::TJsonValue result = SendQuery(query, "classic", base64);
        UNIT_ASSERT_VALUES_EQUAL(result.GetMap().at("column0").GetString(), reply);

        result = SendQuery(query, "ydb", base64);
        UNIT_ASSERT_VALUES_EQUAL(result.GetMap().at("result").GetArray()[0].GetMap().at("column0").GetString(), reply);

        result = SendQuery(query, "modern", base64);
        UNIT_ASSERT_VALUES_EQUAL(result.GetMap().at("result").GetArray()[0].GetArray()[0].GetString(), reply);

        result = SendQuery(query, "multi", base64);
        UNIT_ASSERT_VALUES_EQUAL(result.GetMap().at("result").GetArray()[0].GetMap().at("rows").GetArray()[0].GetArray()[0].GetString(), reply);
    }

    Y_UNIT_TEST(SelectStringWithBase64Encoding)
    {
        QueryTest("select \"Hello\"", true, "SGVsbG8=");
    }

    Y_UNIT_TEST(SelectStringWithNoBase64Encoding)
    {
        QueryTest("select \"Hello\"", false, "Hello");
    }

    void StorageSpaceTest(const TString& withValue, const NKikimrWhiteboard::EFlag diskSpace, const ui64 used, const ui64 limit, const bool isExpectingGroup) {
        TPortManager tp;
        ui16 port = tp.GetPort(2134);
        ui16 grpcPort = tp.GetPort(2135);
        auto settings = TServerSettings(port);
        settings.InitKikimrRunConfig()
                .SetNodeCount(1)
                .SetUseRealThreads(false)
                .SetDomainName("Root");
        TServer server(settings);
        server.EnableGRpc(grpcPort);
        TClient client(settings);
        TTestActorRuntime& runtime = *server.GetRuntime();

        TActorId sender = runtime.AllocateEdgeActor();
        TAutoPtr<IEventHandle> handle;

        THttpRequest httpReq(HTTP_METHOD_GET);
        httpReq.CgiParameters.emplace("with", withValue);
        httpReq.CgiParameters.emplace("version", "v2");
        auto page = MakeHolder<TMonPage>("viewer", "title");
        TMonService2HttpRequest monReq(nullptr, &httpReq, nullptr, page.Get(), "/json/storage", nullptr);
        auto request = MakeHolder<NMon::TEvHttpInfo>(monReq);

        auto observerFunc = [&](TAutoPtr<IEventHandle>& ev) {
            Y_UNUSED(ev);
            if (ev->GetTypeRewrite() == TEvWhiteboard::EvVDiskStateResponse) {
                auto *x = reinterpret_cast<TEvWhiteboard::TEvVDiskStateResponse::TPtr*>(&ev);
                ChangeVDiskStateResponse(x, diskSpace, used, limit);
            }

            return TTestActorRuntime::EEventAction::PROCESS;
        };
        runtime.SetObserverFunc(observerFunc);

        runtime.Send(new IEventHandle(NKikimr::NViewer::MakeViewerID(0), sender, request.Release(), 0));
        NMon::TEvHttpInfoRes* result = runtime.GrabEdgeEvent<NMon::TEvHttpInfoRes>(handle);

        size_t pos = result->Answer.find('{');
        TString jsonResult = result->Answer.substr(pos);
        Ctest << "json result: " << jsonResult << Endl;
        NJson::TJsonValue json;
        try {
            NJson::ReadJsonTree(jsonResult, &json, true);
        }
        catch (yexception ex) {
            Ctest << ex.what() << Endl;
        }
        UNIT_ASSERT_VALUES_EQUAL(json.GetMap().contains("StorageGroups"), isExpectingGroup);
    }

    Y_UNIT_TEST(StorageGroupOutputWithoutFilterNoDepends)
    {
        StorageSpaceTest("all", NKikimrWhiteboard::EFlag::Green, 10, 100, true);
        StorageSpaceTest("all", NKikimrWhiteboard::EFlag::Red, 90, 100, true);
    }

    Y_UNIT_TEST(StorageGroupOutputWithSpaceCheckDependsOnVDiskSpaceStatus)
    {
        StorageSpaceTest("space", NKikimrWhiteboard::EFlag::Green, 10, 100, false);
        StorageSpaceTest("space", NKikimrWhiteboard::EFlag::Red, 10, 100, true);
    }

    Y_UNIT_TEST(StorageGroupOutputWithSpaceCheckDependsOnUsage)
    {
        StorageSpaceTest("space", NKikimrWhiteboard::EFlag::Green, 70, 100, false);
        StorageSpaceTest("space", NKikimrWhiteboard::EFlag::Green, 80, 100, true);
        StorageSpaceTest("space", NKikimrWhiteboard::EFlag::Green, 90, 100, true);
    }

    Y_UNIT_TEST(ServerlessNodesPage)
    {
        TPortManager tp;
        ui16 port = tp.GetPort(2134);
        ui16 grpcPort = tp.GetPort(2135);
        auto settings = TServerSettings(port)
                .SetNodeCount(1)
                .SetDynamicNodeCount(1)
                .SetUseRealThreads(false)
                .SetDomainName("Root")
                .InitKikimrRunConfig();
        TServer server(settings);
        server.EnableGRpc(grpcPort);

        TClient client(settings);
        TTestActorRuntime& runtime = *server.GetRuntime();

        TActorId sender = runtime.AllocateEdgeActor();
        TAutoPtr<IEventHandle> handle;

        THttpRequest httpReq(HTTP_METHOD_GET);
        httpReq.CgiParameters.emplace("tenant", "/Root/serverless");
        httpReq.CgiParameters.emplace("tablets", "true");
        httpReq.CgiParameters.emplace("enums", "true");
        httpReq.CgiParameters.emplace("sort", "");
        httpReq.CgiParameters.emplace("type", "any");
        auto page = MakeHolder<TMonPage>("viewer", "title");
        TMonService2HttpRequest monReq(nullptr, &httpReq, nullptr, page.Get(), "/json/nodes", nullptr);
        auto request = MakeHolder<NMon::TEvHttpInfo>(monReq);

        runtime.Send(new IEventHandle(NKikimr::NViewer::MakeViewerID(0), sender, request.Release(), 0));
        NMon::TEvHttpInfoRes* result = runtime.GrabEdgeEvent<NMon::TEvHttpInfoRes>(handle);

        size_t pos = result->Answer.find('{');
        TString jsonResult = result->Answer.substr(pos);
        Ctest << "json result: " << jsonResult << Endl;
        NJson::TJsonValue json;
        try {
            NJson::ReadJsonTree(jsonResult, &json, true);
        }
        catch (yexception ex) {
            Ctest << ex.what() << Endl;
        }
        UNIT_ASSERT_VALUES_EQUAL(json.GetMap().at("TotalNodes"), "0");
        UNIT_ASSERT_VALUES_EQUAL(json.GetMap().at("FoundNodes"), "0");
    }

    Y_UNIT_TEST(ServerlessWithExclusiveNodes)
    {
        TPortManager tp;
        ui16 port = tp.GetPort(2134);
        ui16 grpcPort = tp.GetPort(2135);
        auto settings = TServerSettings(port)
                .SetNodeCount(1)
                .SetDynamicNodeCount(2)
                .SetUseRealThreads(false)
                .SetDomainName("Root")
                .InitKikimrRunConfig();
        TServer server(settings);
        server.EnableGRpc(grpcPort);

        TClient client(settings);
        TTestActorRuntime& runtime = *server.GetRuntime();

        TActorId sender = runtime.AllocateEdgeActor();
        TAutoPtr<IEventHandle> handle;

        THttpRequest httpReq(HTTP_METHOD_GET);
        httpReq.CgiParameters.emplace("tenant", "/Root/serverless");
        httpReq.CgiParameters.emplace("tablets", "true");
        httpReq.CgiParameters.emplace("enums", "true");
        httpReq.CgiParameters.emplace("sort", "");
        httpReq.CgiParameters.emplace("type", "any");
        auto page = MakeHolder<TMonPage>("viewer", "title");
        TMonService2HttpRequest monReq(nullptr, &httpReq, nullptr, page.Get(), "/json/nodes", nullptr);
        auto request = MakeHolder<NMon::TEvHttpInfo>(monReq);

        size_t staticNodeId = 0;
        size_t sharedDynNodeId = 0;
        size_t exclusiveDynNodeId = 0;    
        auto observerFunc = [&](TAutoPtr<IEventHandle>& ev) {
            switch (ev->GetTypeRewrite()) {
                case TEvInterconnect::EvNodesInfo: {
                    auto *x = reinterpret_cast<TEvInterconnect::TEvNodesInfo::TPtr*>(&ev);
                    TVector<TEvInterconnect::TNodeInfo> &nodes = (*x)->Get()->Nodes;
                    UNIT_ASSERT_EQUAL(nodes.size(), 3);
                    staticNodeId = nodes[0];
                    sharedDynNodeId = nodes[1];
                    exclusiveDynNodeId = nodes[2];
                    break;
                }
                case TEvStateStorage::EvBoardInfo: {
                    auto *x = reinterpret_cast<TEvStateStorage::TEvBoardInfo::TPtr*>(&ev);
                    auto *record = (*x)->Get();
                    using EStatus = TEvStateStorage::TEvBoardInfo::EStatus;
                    const_cast<EStatus&>(record->Status) = EStatus::Ok;
                    TActorId actorOnExclusiveDynNode = TActorId(exclusiveDynNodeId, 0, 0, 0);
                    record->InfoEntries[actorOnExclusiveDynNode] = {};
                    break;
                }
            }

            return TTestActorRuntime::EEventAction::PROCESS;
        };
        runtime.SetObserverFunc(observerFunc);

        runtime.Send(new IEventHandle(NKikimr::NViewer::MakeViewerID(0), sender, request.Release(), 0));
        NMon::TEvHttpInfoRes* result = runtime.GrabEdgeEvent<NMon::TEvHttpInfoRes>(handle);

        size_t pos = result->Answer.find('{');
        TString jsonResult = result->Answer.substr(pos);
        Ctest << "json result: " << jsonResult << Endl;
        NJson::TJsonValue json;
        try {
            NJson::ReadJsonTree(jsonResult, &json, true);
        }
        catch (yexception ex) {
            Ctest << ex.what() << Endl;
        }
        UNIT_ASSERT_VALUES_EQUAL(json.GetMap().at("TotalNodes"), "1");
        UNIT_ASSERT_VALUES_EQUAL(json.GetMap().at("FoundNodes"), "1");
        UNIT_ASSERT_VALUES_EQUAL(json.GetMap().at("Nodes").GetArray().size(), 1);
        auto node = json.GetMap().at("Nodes").GetArray()[0].GetMap();
        UNIT_ASSERT_VALUES_EQUAL(node.at("NodeId"), exclusiveDynNodeId);
    }

    Y_UNIT_TEST(SharedDoesntShowExclusiveNodes)
    {
        TPortManager tp;
        ui16 port = tp.GetPort(2134);
        ui16 grpcPort = tp.GetPort(2135);
        auto settings = TServerSettings(port)
                .SetNodeCount(1)
                .SetDynamicNodeCount(2)
                .SetUseRealThreads(false)
                .SetDomainName("Root")
                .InitKikimrRunConfig();
        TServer server(settings);
        server.EnableGRpc(grpcPort);

        TClient client(settings);
        TTestActorRuntime& runtime = *server.GetRuntime();

        TActorId sender = runtime.AllocateEdgeActor();
        TAutoPtr<IEventHandle> handle;

        THttpRequest httpReq(HTTP_METHOD_GET);
        httpReq.CgiParameters.emplace("tenant", "Root/shared");
        httpReq.CgiParameters.emplace("tablets", "true");
        httpReq.CgiParameters.emplace("enums", "true");
        httpReq.CgiParameters.emplace("sort", "");
        httpReq.CgiParameters.emplace("type", "any");
        auto page = MakeHolder<TMonPage>("viewer", "title");
        TMonService2HttpRequest monReq(nullptr, &httpReq, nullptr, page.Get(), "/json/nodes", nullptr);
        auto request = MakeHolder<NMon::TEvHttpInfo>(monReq);

        size_t staticNodeId = 0;
        size_t sharedDynNodeId = 0;
        size_t exclusiveDynNodeId = 0; 
        auto observerFunc = [&](TAutoPtr<IEventHandle>& ev) {
            switch (ev->GetTypeRewrite()) {
                case TEvInterconnect::EvNodesInfo: {
                    auto *x = reinterpret_cast<TEvInterconnect::TEvNodesInfo::TPtr*>(&ev);
                    TVector<TEvInterconnect::TNodeInfo> &nodes = (*x)->Get()->Nodes;
                    UNIT_ASSERT_EQUAL(nodes.size(), 3);
                    staticNodeId = nodes[0];
                    sharedDynNodeId = nodes[1];
                    exclusiveDynNodeId = nodes[2];
                    break;
                }
                case TEvStateStorage::EvBoardInfo: {
                    auto *x = reinterpret_cast<TEvStateStorage::TEvBoardInfo::TPtr*>(&ev);
                    auto *record = (*x)->Get();
                    using EStatus = TEvStateStorage::TEvBoardInfo::EStatus;
                    const_cast<EStatus&>(record->Status) = EStatus::Ok;
                    TActorId actorOnSharedDynNode = TActorId(sharedDynNodeId, 0, 0, 0);
                    record->InfoEntries[actorOnSharedDynNode] = {};
                    break;
                }
            }

            return TTestActorRuntime::EEventAction::PROCESS;
        };
        runtime.SetObserverFunc(observerFunc);

        runtime.Send(new IEventHandle(NKikimr::NViewer::MakeViewerID(0), sender, request.Release(), 0));
        NMon::TEvHttpInfoRes* result = runtime.GrabEdgeEvent<NMon::TEvHttpInfoRes>(handle);

        size_t pos = result->Answer.find('{');
        TString jsonResult = result->Answer.substr(pos);
        Ctest << "json result: " << jsonResult << Endl;
        NJson::TJsonValue json;
        try {
            NJson::ReadJsonTree(jsonResult, &json, true);
        }
        catch (yexception ex) {
            Ctest << ex.what() << Endl;
        }
        UNIT_ASSERT_VALUES_EQUAL(json.GetMap().at("TotalNodes"), "1");
        UNIT_ASSERT_VALUES_EQUAL(json.GetMap().at("FoundNodes"), "1");
        UNIT_ASSERT_VALUES_EQUAL(json.GetMap().at("Nodes").GetArray().size(), 1);
        auto node = json.GetMap().at("Nodes").GetArray()[0].GetMap();
        UNIT_ASSERT_VALUES_EQUAL(node.at("NodeId"), sharedDynNodeId);
    }

    Y_UNIT_TEST(ServerlessWithExclusiveNodesCheckTable)
    {
        TPortManager tp;
        ui16 port = tp.GetPort(2134);
        ui16 grpcPort = tp.GetPort(2135);
        auto settings = TServerSettings(port)
                .SetNodeCount(1)
                .SetDynamicNodeCount(3)
                .SetUseRealThreads(false)
                .SetDomainName("Root")
                .InitKikimrRunConfig();
        TServer server(settings);
        server.EnableGRpc(grpcPort);

        TClient client(settings);
        TTestActorRuntime& runtime = *server.GetRuntime();

        TActorId sender = runtime.AllocateEdgeActor();
        TAutoPtr<IEventHandle> handle;

        THttpRequest httpReq(HTTP_METHOD_GET);
        httpReq.CgiParameters.emplace("tenant", "/Root/serverless");
        httpReq.CgiParameters.emplace("path", "/Root/serverless/users");
        httpReq.CgiParameters.emplace("tablets", "true");
        httpReq.CgiParameters.emplace("enums", "true");
        httpReq.CgiParameters.emplace("sort", "");
        httpReq.CgiParameters.emplace("type", "any");
        auto page = MakeHolder<TMonPage>("viewer", "title");
        TMonService2HttpRequest monReq(nullptr, &httpReq, nullptr, page.Get(), "/json/nodes", nullptr);
        auto request = MakeHolder<NMon::TEvHttpInfo>(monReq);

        const TPathId SERVERLESS_DOMAIN_KEY = {7000000000, 2};
        const TPathId SHARED_DOMAIN_KEY = {7000000000, 1};
        const TPathId SERVERLESS_TABLE = {7000000001, 2};

        size_t staticNodeId = 0;
        size_t sharedDynNodeId = 0;
        size_t exclusiveDynNodeId = 0;
        size_t secondExclusiveDynNodeId = 0;    
        auto observerFunc = [&](TAutoPtr<IEventHandle>& ev) {
            switch (ev->GetTypeRewrite()) {
                case TEvTxProxySchemeCache::EvNavigateKeySetResult: {
                    auto *x = reinterpret_cast<TEvTxProxySchemeCache::TEvNavigateKeySetResult::TPtr*>(&ev);
                    TSchemeCacheNavigate::TEntry& entry((*x)->Get()->Request->ResultSet.front());
                    TString path = CanonizePath(entry.Path);
                    if (path == "/Root/serverless" || entry.TableId.PathId == SERVERLESS_DOMAIN_KEY) {
                        entry.Status = TSchemeCacheNavigate::EStatus::Ok;
                        entry.Kind = TSchemeCacheNavigate::EKind::KindExtSubdomain;
                        entry.DomainInfo = MakeIntrusive<TDomainInfo>(SERVERLESS_DOMAIN_KEY, SHARED_DOMAIN_KEY);
                    } else if (path == "/Root/shared" || entry.TableId.PathId == SHARED_DOMAIN_KEY) {
                        entry.Status = TSchemeCacheNavigate::EStatus::Ok;
                        entry.Kind = TSchemeCacheNavigate::EKind::KindExtSubdomain;
                        entry.DomainInfo = MakeIntrusive<TDomainInfo>(SHARED_DOMAIN_KEY, SHARED_DOMAIN_KEY);
                        auto domains = runtime.GetAppData().DomainsInfo;
                        auto domain = domains->Domains.begin()->second;
                        ui64 hiveId = domains->GetHive(domain->DefaultHiveUid);
                        entry.DomainInfo->Params.SetHive(hiveId);
                    } else if (path == "/Root/serverless/users" || entry.TableId.PathId == SERVERLESS_TABLE) {
                        entry.Status = TSchemeCacheNavigate::EStatus::Ok;
                        entry.Kind = TSchemeCacheNavigate::EKind::KindTable;
                        entry.DomainInfo = MakeIntrusive<TDomainInfo>(SERVERLESS_DOMAIN_KEY, SHARED_DOMAIN_KEY);
                        auto dirEntryInfo = MakeIntrusive<TSchemeCacheNavigate::TDirEntryInfo>();
                        dirEntryInfo->Info.SetSchemeshardId(SERVERLESS_TABLE.OwnerId);
                        dirEntryInfo->Info.SetPathId(SERVERLESS_TABLE.LocalPathId);
                        entry.Self = dirEntryInfo;
                    }
                    break;
                }
                case TEvInterconnect::EvNodesInfo: {
                    auto *x = reinterpret_cast<TEvInterconnect::TEvNodesInfo::TPtr*>(&ev);
                    TVector<TEvInterconnect::TNodeInfo> &nodes = (*x)->Get()->Nodes;
                    UNIT_ASSERT_EQUAL(nodes.size(), 4);
                    staticNodeId = nodes[0];
                    sharedDynNodeId = nodes[1];
                    exclusiveDynNodeId = nodes[2];
                    secondExclusiveDynNodeId = nodes[3];
                    break;
                }
                case TEvStateStorage::EvBoardInfo: {
                    auto *x = reinterpret_cast<TEvStateStorage::TEvBoardInfo::TPtr*>(&ev);
                    auto *record = (*x)->Get();
                    using EStatus = TEvStateStorage::TEvBoardInfo::EStatus;
                    const_cast<EStatus&>(record->Status) = EStatus::Ok;
                    TActorId actorOnExclusiveDynNode = TActorId(exclusiveDynNodeId, 0, 0, 0);
                    record->InfoEntries[actorOnExclusiveDynNode] = {};
                    TActorId actorOnSecondExclusiveDynNode = TActorId(secondExclusiveDynNodeId, 0, 0, 0);
                    record->InfoEntries[actorOnSecondExclusiveDynNode] = {};
                    break;
                }
                case TEvHive::EvResponseHiveNodeStats: {
                    auto *x = reinterpret_cast<TEvHive::TEvResponseHiveNodeStats::TPtr*>(&ev);
                    auto &record = (*x)->Get()->Record;
                    auto *sharedNodeStats = record.MutableNodeStats()->Add();
                    sharedNodeStats->SetNodeId(sharedDynNodeId);
                    sharedNodeStats->MutableNodeDomain()->SetSchemeShard(SHARED_DOMAIN_KEY.OwnerId);
                    sharedNodeStats->MutableNodeDomain()->SetPathId(SHARED_DOMAIN_KEY.LocalPathId);

                    auto *exclusiveNodeStats = record.MutableNodeStats()->Add();
                    exclusiveNodeStats->SetNodeId(exclusiveDynNodeId);
                    exclusiveNodeStats->MutableNodeDomain()->SetSchemeShard(SERVERLESS_DOMAIN_KEY.OwnerId);
                    exclusiveNodeStats->MutableNodeDomain()->SetPathId(SERVERLESS_DOMAIN_KEY.LocalPathId);

                    auto *secondExclusiveNodeStats = record.MutableNodeStats()->Add();
                    secondExclusiveNodeStats->SetNodeId(secondExclusiveDynNodeId);
                    secondExclusiveNodeStats->MutableNodeDomain()->SetSchemeShard(SERVERLESS_DOMAIN_KEY.OwnerId);
                    secondExclusiveNodeStats->MutableNodeDomain()->SetPathId(SERVERLESS_DOMAIN_KEY.LocalPathId);

                    // filtered one datashard from /Root/serverless/users
                    auto *stateStats = secondExclusiveNodeStats->MutableStateStats()->Add();
                    stateStats->SetTabletType(NKikimrTabletBase::TTabletTypes::DataShard);
                    stateStats->SetVolatileState(NKikimrHive::TABLET_VOLATILE_STATE_RUNNING);
                    stateStats->SetCount(1);
                    break;
                }
            }

            return TTestActorRuntime::EEventAction::PROCESS;
        };
        runtime.SetObserverFunc(observerFunc);

        runtime.Send(new IEventHandle(NKikimr::NViewer::MakeViewerID(0), sender, request.Release(), 0));
        NMon::TEvHttpInfoRes* result = runtime.GrabEdgeEvent<NMon::TEvHttpInfoRes>(handle);

        size_t pos = result->Answer.find('{');
        TString jsonResult = result->Answer.substr(pos);
        Ctest << "json result: " << jsonResult << Endl;
        NJson::TJsonValue json;
        try {
            NJson::ReadJsonTree(jsonResult, &json, true);
        }
        catch (yexception ex) {
            Ctest << ex.what() << Endl;
        }
        UNIT_ASSERT_VALUES_EQUAL(json.GetMap().at("TotalNodes"), "2");
        UNIT_ASSERT_VALUES_EQUAL(json.GetMap().at("FoundNodes"), "2");
        UNIT_ASSERT_VALUES_EQUAL(json.GetMap().at("Nodes").GetArray().size(), 2);
        auto firstNode = json.GetMap().at("Nodes").GetArray()[0].GetMap();
        UNIT_ASSERT_VALUES_EQUAL(firstNode.at("NodeId"), exclusiveDynNodeId);
        UNIT_ASSERT(!firstNode.contains("Tablets"));
        auto secondNode = json.GetMap().at("Nodes").GetArray()[1].GetMap();
        UNIT_ASSERT_VALUES_EQUAL(secondNode.at("NodeId"), secondExclusiveDynNodeId);
        UNIT_ASSERT_VALUES_EQUAL(secondNode.at("Tablets").GetArray().size(), 1);
        auto tablet = secondNode.at("Tablets").GetArray()[0].GetMap();
        UNIT_ASSERT_VALUES_EQUAL(tablet.at("Type"), "DataShard");
        UNIT_ASSERT_VALUES_EQUAL(tablet.at("State"), "Green");
        UNIT_ASSERT_VALUES_EQUAL(tablet.at("Count"), 1);
    }
}
