#include "with_blobs.h"

namespace NKikimr::NOlap {

const TColumnRecord& TPortionInfoWithBlobs::TBlobInfo::AddChunk(TPortionInfoWithBlobs& owner, TOrderedColumnChunk&& chunk) {
    Y_VERIFY(!ResultBlob);
    const ui32 columnId = chunk.GetColumnId();
    auto rec = TColumnRecord::Make(columnId, owner.ColumnChunkIds[columnId]++);
    rec.BlobRange.Offset = Size;
    rec.BlobRange.Size = chunk.GetData().size();
    auto& result = owner.PortionInfo.AppendOneChunkColumn(std::move(rec));
    Size += chunk.GetData().size();
    Chunks.emplace_back(std::move(chunk));
    return result;
}

void TPortionInfoWithBlobs::TBlobInfo::RegisterBlobId(TPortionInfoWithBlobs& owner, const TUnifiedBlobId& blobId) {
    Y_VERIFY(StartRecordsIndex + Chunks.size() <= owner.PortionInfo.Records.size());
    for (ui32 idx = 0; idx < Chunks.size(); ++idx) {
        owner.PortionInfo.Records[StartRecordsIndex + idx].BlobRange.BlobId = blobId;
    }
}

}