#include "map/country_tree.hpp"

#include "map/framework.hpp"

#include "storage/storage.hpp"

namespace storage
{

namespace
{
int const kRootItemIndex = 0;
int const kChildItemsOffset = 1;

inline TIndex GetIndexChild(TIndex const & index, int i)
{
  TIndex child(index);
  if (child.m_group == TIndex::INVALID)
    child.m_group = i;
  else if (child.m_country == TIndex::INVALID)
    child.m_country = i;
  else if (child.m_region == TIndex::INVALID)
    child.m_region = i;

  return child;
}

inline TIndex GetIndexParent(TIndex const & index)
{
  ASSERT(index != TIndex(), ());
  TIndex parent(index);
  if (parent.m_region != TIndex::INVALID)
    parent.m_region = TIndex::INVALID;
  else if (parent.m_country != TIndex::INVALID)
    parent.m_country = TIndex::INVALID;
  else
    parent.m_group = TIndex::INVALID;

  return parent;
}
}  // namespace

CountryTree::CountryTree(shared_ptr<ActiveMapsLayout> activeMaps)
{
  m_layout = activeMaps;
  ConnectToCoreStorage();
}

CountryTree::~CountryTree()
{
  DisconnectFromCoreStorage();
}

CountryTree & CountryTree::operator=(CountryTree const & other)
{
  if (this == &other)
    return *this;

  DisconnectFromCoreStorage();
  m_levelItems = other.m_levelItems;
  m_listener = other.m_listener;
  m_layout = other.m_layout;
  ConnectToCoreStorage();

  return *this;
}

ActiveMapsLayout & CountryTree::GetActiveMapLayout()
{
  ASSERT(IsValid(), ());
  return *m_layout;
}

ActiveMapsLayout const & CountryTree::GetActiveMapLayout() const
{
  ASSERT(IsValid(), ());
  return *m_layout;
}

bool CountryTree::IsValid() const
{
  return m_layout != nullptr;
}

void CountryTree::SetDefaultRoot()
{
  SetRoot(TIndex());
}

void CountryTree::SetParentAsRoot()
{
  ASSERT(HasParent(), ());
  SetRoot(GetIndexParent(GetCurrentRoot()));
}

void CountryTree::SetChildAsRoot(int childPosition)
{
  ASSERT(!IsLeaf(childPosition), ());
  SetRoot(GetChild(childPosition));
}

void CountryTree::ResetRoot()
{
  m_levelItems.clear();
}

bool CountryTree::HasRoot() const
{
  return !m_levelItems.empty();
}

bool CountryTree::HasParent() const
{
  return GetCurrentRoot() != TIndex();
}

int CountryTree::GetChildCount() const
{
  ASSERT(HasRoot(), ());
  return m_levelItems.size() - kChildItemsOffset;
}

bool CountryTree::IsLeaf(int childPosition) const
{
  return GetStorage().IsLeaf(GetChild(childPosition));
}

string const & CountryTree::GetChildName(int position) const
{
  return GetStorage().CountryName(GetChild(position));
}

TStatus CountryTree::GetLeafStatus(int position) const
{
  return GetStorage().CountryStatusEx(GetChild(position));
}

MapOptions CountryTree::GetLeafOptions(int position) const
{
  TStatus status;
  MapOptions options;
  GetStorage().CountryStatusEx(GetChild(position), status, options);
  return options;
}

LocalAndRemoteSizeT const CountryTree::GetDownloadableLeafSize(int position) const
{
  return GetActiveMapLayout().GetDownloadableCountrySize(GetChild(position));
}

LocalAndRemoteSizeT const CountryTree::GetLeafSize(int position, MapOptions const & options) const
{
  return GetActiveMapLayout().GetCountrySize(GetChild(position), options);
}

LocalAndRemoteSizeT const CountryTree::GetRemoteLeafSizes(int position) const
{
  return GetActiveMapLayout().GetRemoteCountrySizes(GetChild(position));
}

bool CountryTree::IsCountryRoot() const
{
  TIndex index = GetCurrentRoot();
  ASSERT(index.m_region == TIndex::INVALID, ());
  if (index.m_country != TIndex::INVALID)
    return true;

  return false;
}

string const & CountryTree::GetRootName() const
{
  return GetStorage().CountryName(GetCurrentRoot());
}

void CountryTree::DownloadCountry(int childPosition, MapOptions const & options)
{
  ASSERT(IsLeaf(childPosition), ());
  GetActiveMapLayout().DownloadMap(GetChild(childPosition), options);
}

void CountryTree::DeleteCountry(int childPosition, MapOptions const & options)
{
  ASSERT(IsLeaf(childPosition), ());
  GetActiveMapLayout().DeleteMap(GetChild(childPosition), options);
}

void CountryTree::RetryDownloading(int childPosition)
{
  ASSERT(IsLeaf(childPosition), ());
  GetActiveMapLayout().RetryDownloading(GetChild(childPosition));
}

void CountryTree::DownloadAllImpl(TIndex const & localRootIndex)
{
  if (GetStorage().IsLeaf(localRootIndex))
  {
    GetActiveMapLayout().DownloadMap(localRootIndex, MapOptions::Map);
    return;
  }

  size_t const childCount = GetStorage().CountriesCount(localRootIndex);
  for (size_t i = 0; i < childCount; ++i)
  {
    TIndex const child = GetIndexChild(localRootIndex, i);
    ASSERT_NOT_EQUAL(localRootIndex, child, ());
    DownloadAllImpl(child);
  }
}

void CountryTree::DownloadAll()
{
  DownloadAllImpl(GetCurrentRoot());
}

bool CountryTree::IsDownloadAllAvailable()
{
  return true;
}

void CountryTree::CancelDownloading(int childPosition)
{
  GetStorage().DeleteFromDownloader(GetChild(childPosition));
}

void CountryTree::SetListener(CountryTreeListener * listener)
{
  m_listener = listener;
}

void CountryTree::ResetListener()
{
  m_listener = nullptr;
}

void CountryTree::ShowLeafOnMap(int position)
{
  ASSERT(IsLeaf(position), ());
  GetActiveMapLayout().ShowMap(GetChild(position));
}

Storage const & CountryTree::GetStorage() const
{
  ASSERT(IsValid(), ());
  return m_layout->GetStorage();
}

Storage & CountryTree::GetStorage()
{
  ASSERT(IsValid(), ());
  return m_layout->GetStorage();
}

void CountryTree::ConnectToCoreStorage()
{
  if (IsValid())
  {
    m_subscribeSlotID = GetStorage().Subscribe(bind(&CountryTree::NotifyStatusChanged, this, _1),
                                               bind(&CountryTree::NotifyProgressChanged, this, _1, _2));
  }
}

void CountryTree::DisconnectFromCoreStorage()
{
  if (IsValid())
    GetStorage().Unsubscribe(m_subscribeSlotID);
}

TIndex const & CountryTree::GetCurrentRoot() const
{
  ASSERT(HasRoot(), ());
  return m_levelItems[kRootItemIndex];
}

void CountryTree::SetRoot(TIndex index)
{
  ResetRoot();

  size_t const count = GetStorage().CountriesCount(index);
  m_levelItems.reserve(kChildItemsOffset + count);
  m_levelItems.push_back(index);
  for (size_t i = 0; i < count; ++i)
    m_levelItems.push_back(GetIndexChild(index, i));
}

TIndex const & CountryTree::GetChild(int childPosition) const
{
  ASSERT(childPosition < GetChildCount(), ());
  return m_levelItems[kChildItemsOffset + childPosition];
}

int CountryTree::GetChildPosition(TIndex const & index)
{
  int result = -1;
  if (HasRoot())
  {
    auto iter = find(m_levelItems.begin(), m_levelItems.end(), index);
    if (iter != m_levelItems.end())
      result = distance(m_levelItems.begin(), iter) - kChildItemsOffset;
  }

  return result;
}

void CountryTree::NotifyStatusChanged(TIndex const & index)
{
  if (m_listener != nullptr)
  {
    int position = GetChildPosition(index);
    if (position != -1)
      m_listener->ItemStatusChanged(position);
  }
}

void CountryTree::NotifyProgressChanged(TIndex const & index, LocalAndRemoteSizeT const & sizes)
{
  if (m_listener != nullptr)
  {
    int position = GetChildPosition(index);
    if (position != -1)
      m_listener->ItemProgressChanged(position, sizes);
  }
}

}
