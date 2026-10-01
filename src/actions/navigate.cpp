#include "navigate.hpp"

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>

using namespace geode::prelude;

// Wait for the quit transition out of a level before opening something
static constexpr float AFTER_QUIT_DELAY = .8f;

static void pushScene(CCScene *scene)
{
  CCDirector::get()->pushScene(CCTransitionFade::create(.5f, scene));
}

static void openBrowser(GJSearchObject *search)
{
  pushScene(LevelBrowserLayer::scene(search));
}

// ! --- Online search --- !

// GameLevelManager has one delegate slot, borrow it for a single search.
// Found level opens its page, list opens the list, user opens the profile, otherwise the search results
class SearchOpener : public CCNode, public LevelManagerDelegate
{
private:
  Ref<GJSearchObject> m_search;
  LevelManagerDelegate *m_previousDelegate = nullptr;
  std::string m_key;

public:
  static void start(GJSearchObject *search)
  {
    auto ret = new SearchOpener();
    ret->m_search = search;
    ret->m_key = search->getKey();

    auto glm = GameLevelManager::get();
    ret->m_previousDelegate = glm->m_levelManagerDelegate;
    glm->m_levelManagerDelegate = ret;

    if (search->m_searchType == SearchType::Users)
      glm->getUsers(search);
    else if (search->m_searchMode == 1)
      glm->getLevelLists(search);
    else
      glm->getOnlineLevels(search);
  }

  void loadLevelsFinished(CCArray *items, char const *key) override
  {
    if (!key || key != m_key)
      return;

    auto first = items && items->count() ? items->objectAtIndex(0) : nullptr;

    if (auto level = typeinfo_cast<GJGameLevel *>(first))
      pushScene(LevelInfoLayer::scene(level, false));
    else if (auto list = typeinfo_cast<GJLevelList *>(first))
      pushScene(LevelListLayer::scene(list));
    else if (auto user = typeinfo_cast<GJUserScore *>(first))
      ProfilePage::create(user->m_accountID, false)->show();
    else
      openBrowser(m_search);

    finish();
  }

  void loadLevelsFinished(CCArray *items, char const *key, int) override
  {
    loadLevelsFinished(items, key);
  }

  void loadLevelsFailed(char const *key) override
  {
    if (!key || key != m_key)
      return;

    Notification::create("Nothing found", NotificationIcon::Error)->show();
    finish();
  }

  void loadLevelsFailed(char const *key, int) override
  {
    loadLevelsFailed(key);
  }

private:
  void finish()
  {
    auto glm = GameLevelManager::get();
    if (glm->m_levelManagerDelegate == this)
      glm->m_levelManagerDelegate = m_previousDelegate;

    release();
  }
};

// ! --- Filters --- !

static std::optional<SearchType> getBrowserType(std::string const &list)
{
  if (list == "most_downloaded") return SearchType::Downloaded;
  if (list == "most_liked") return SearchType::MostLiked;
  if (list == "trending") return SearchType::Trending;
  if (list == "recent") return SearchType::Recent;
  if (list == "featured") return SearchType::Featured;
  if (list == "awarded") return SearchType::Awarded;
  if (list == "magic") return SearchType::Magic;
  if (list == "hall_of_fame") return SearchType::HallOfFame;
  return std::nullopt;
}

// GD search takes difficulties and lengths as "1,2,3"
static std::string joinCodes(
    matjson::Value const &names, std::initializer_list<std::pair<char const *, int>> codes)
{
  std::vector<std::string> parts;
  if (!names.isArray())
    return "-";

  for (auto const &name : names)
  {
    auto value = name.asString().unwrapOr("");
    for (auto const &[codeName, code] : codes)
    {
      if (value == codeName)
        parts.push_back(std::to_string(code));
    }
  }

  return parts.empty() ? "-" : utils::string::join(parts, ",");
}

static GJSearchObject *createFilteredSearch(matjson::Value const &data)
{
  auto difficulty = joinCodes(data["difficulties"], {
                                                        {"na", -1},
                                                        {"auto", -3},
                                                        {"easy", 1},
                                                        {"normal", 2},
                                                        {"hard", 3},
                                                        {"harder", 4},
                                                        {"insane", 5},
                                                        {"demon", -2},
                                                    });
  auto length = joinCodes(data["lengths"], {
                                               {"tiny", 0},
                                               {"short", 1},
                                               {"medium", 2},
                                               {"long", 3},
                                               {"xl", 4},
                                               {"platformer", 5},
                                           });

  static constexpr std::array DEMONS = {"any", "easy", "medium", "hard", "insane", "extreme"};
  auto demonName = data["demon"].asString().unwrapOr("any");
  // 0 is any demon, then easy..extreme are 1..5
  int demonFilter = std::find(DEMONS.begin(), DEMONS.end(), demonName) - DEMONS.begin();
  if (demonFilter >= static_cast<int>(DEMONS.size()) || difficulty != "-2")
    demonFilter = 0;

  auto flag = [&data](char const *key)
  { return data[key].asBool().unwrapOr(false); };

  return GJSearchObject::create(
      SearchType::Search, data["query"].asString().unwrapOr(""), difficulty, length,
      0, flag("rated"), flag("uncompleted"), flag("featured"), 0, flag("original"),
      flag("twoPlayer"), false, false, false, flag("coins"), flag("epic"),
      flag("legendary"), flag("mythic"), false, demonFilter, 0, 0);
}

static GJSearchObject *createListSearch(std::string const &query)
{
  auto search = GJSearchObject::create(SearchType::Search, query);
  search->m_searchMode = 1;
  return search;
}

static void openSong(int songId)
{
  auto song = MusicDownloadManager::sharedState()->getSongInfoObject(songId);
  auto newgrounds = fmt::format("https://www.newgrounds.com/audio/listen/{}", songId);

  if (!song)
  {
    web::openLinkInBrowser(newgrounds);
    return;
  }

  SongInfoLayer::create(
      song->m_songName, song->m_artistName, song->m_songUrl, newgrounds,
      song->m_youtubeVideo, song->m_youtubeChannel, songId, song->m_shortTagsString,
      song->m_nongType)
      ->show();
}

// ! --- Actions --- !

static std::string joinIds(matjson::Value const &ids)
{
  std::vector<std::string> parts;
  if (!ids.isArray())
    return "";

  for (auto const &id : ids)
    parts.push_back(std::to_string(id.asInt().unwrapOr(0)));
  return utils::string::join(parts, ",");
}

static void open(ChatAction const &action)
{
  auto const &data = action.data;
  auto const &type = action.type;
  auto str = [&data](char const *key)
  { return data[key].asString().unwrapOr(""); };
  auto num = [&data](char const *key)
  { return std::to_string(data[key].asInt().unwrapOr(0)); };

  if (type == "open_level")
    SearchOpener::start(GJSearchObject::create(SearchType::Search, num("levelId")));
  else if (type == "open_profile")
    SearchOpener::start(GJSearchObject::create(SearchType::Users, str("username")));
  else if (type == "open_search")
    openBrowser(GJSearchObject::create(SearchType::Search, str("query")));
  else if (type == "open_filtered_search")
    openBrowser(createFilteredSearch(data));
  else if (type == "open_creator_levels")
    openBrowser(GJSearchObject::create(SearchType::UsersLevels, num("playerId")));
  else if (type == "open_similar")
    openBrowser(GJSearchObject::create(SearchType::Similar, num("levelId")));
  else if (type == "open_map_pack")
  {
    auto ids = joinIds(data["levelIds"]);
    openBrowser(ids.empty()
                    ? GJSearchObject::create(SearchType::MapPack)
                    : GJSearchObject::create(SearchType::MapPackOnClick, ids));
  }
  else if (type == "open_gauntlet")
  {
    int id = data["gauntletId"].asInt().unwrapOr(0);
    pushScene(id > 0 ? GauntletLayer::scene(static_cast<GauntletType>(id))
                     : GauntletSelectLayer::scene(0));
  }
  else if (type == "open_level_list")
  {
    int id = data["listId"].asInt().unwrapOr(0);
    if (id > 0)
      SearchOpener::start(createListSearch(std::to_string(id)));
    else
      openBrowser(createListSearch(str("query")));
  }
  else if (type == "open_song")
    openSong(data["songId"].asInt().unwrapOr(0));
  else if (auto list = getBrowserType(str("list")); type == "open_browser" && list)
    openBrowser(GJSearchObject::create(*list));
}

void actions::run(ChatAction const &action)
{
  // Leaving the editor could lose unsaved work, let the player do it
  if (LevelEditorLayer::get())
  {
    Notification::create("Exit the editor first", NotificationIcon::Warning)->show();
    return;
  }

  // A song opens as a popup, no need to leave the level
  auto play = PlayLayer::get();
  if (!play || action.type == "open_song")
  {
    open(action);
    return;
  }

  createQuickPopup(
      "Leave level?", "Your current attempt will end.", "Stay", "Leave",
      [action](FLAlertLayer *, bool leave)
      {
        auto play = PlayLayer::get();
        if (!leave || !play)
          return;

        play->onQuit();

        // Detached node: a node in the old scene gets its actions paused on exit
        auto delayed = CCSequence::create(
            CCDelayTime::create(AFTER_QUIT_DELAY),
            CallFuncExt::create([action]
                                { open(action); }),
            nullptr);
        CCDirector::get()->getActionManager()->addAction(
            delayed, CCNode::create(), false);
      });
}
