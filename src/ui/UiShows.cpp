#include "UiGameScreen.hpp"
#include "../Shows.hpp"
#include "../Fences.hpp"
#include "../Utils.hpp"
#include "UiLayout.hpp"
#include "UiButton.hpp"
#include "UiListBox.hpp"
#include "UiText.hpp"
#include "UiImage.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
namespace {
std::string showMoney(long cents) { char value[64];std::snprintf(value,sizeof value,"$%.2f",cents/100.0);return value; }
// The frequency list's rows and a show's frequency (ours: -1 never, 0
// continuous, 1 frequent, 2 infrequent): zoo.exe's row i sets the show's
// interval to the table at 0x642c58 - never (-1), infrequentShowInterval,
// frequentShowInterval, continuousShowInterval (showtime_list_click
// 0x6156da), and the panel picks the row whose interval the show has
// (ui_show1_468968)
int showFrequencyOf(int row) { static const int of[4]={-1,2,1,0};return row>=0 && row<4?of[row]:-1; }
int frequencyRow(int frequency) { for(int row=0;row<4;row++)if(showFrequencyOf(row)==frequency)return row;return 0; }
}

void UiGameScreen::setupShows() {
  for(auto &entry:entries)if(entry.id==41)showPanelLayout=entry.layout;
  auto *z=showPanelLayout;if(!z)return;
  auto list=[z](int id){return dynamic_cast<UiListBox*>(z->getElementById(id));};
  auto button=[z](int id,std::function<void()> fn){if(auto *b=dynamic_cast<UiButton*>(z->getElementById(id)))b->onClick=fn;};
  for(int id:{44,45})if(auto *e=z->getElementById(id))e->setHidden(true);
  button(11123,[this,z]{if(auto *e=z->getElementById(45))e->setHidden(!e->isHidden());});
  button(11108,[this,z]{if(auto *e=z->getElementById(44))e->setHidden(!e->isHidden());});
  if(auto *l=list(11196))l->setSelectionAction(static_cast<UiAction>(71001));
  // (the frequencies in the original's order, zoo.exe loadShowSettings
  // 0x5167ca: the rows of the table at 0x642c50, 11192 to 11195 - Never,
  // then each more often; showFrequencyOf / frequencyRow below)
  if(auto *l=list(11191)) {
    l->clear();
    for(int row=0;row<4;row++)l->addItem(resource_manager->getString(11192+row));
    l->setSelectionAction(static_cast<UiAction>(71002));
  }
  // (Add: zoo.exe show1_AddButton_click 0x47594f - a script already
  // maxLength long takes no more, and the message bar says so, 11493 as
  // kind 3; else the trick's added and picked in the script)
  button(11113,[this,list]{if(shows)if(auto *l=list(11111)) {
    if(const auto *s=shows->show(shownShow);s && static_cast<int>(s->script.size())>=shows->settings.maxLength) {
      postMessage(resource_manager->getString(11493),3);return;
    }
    shows->add(shownShow,l->getSelectedData());showListSignature.clear();refreshShow();}});
  button(11114,[this,list]{if(shows)if(auto *l=list(11115)) {shows->remove(shownShow,l->getSelectedIndex());showListSignature.clear();refreshShow();}});
  for(auto [id,by]:{std::pair<int,int>{11117,-1},{11118,1}})button(id,[this,list,by]{
    if(shows)if(auto *l=list(11115))if(shows->move(shownShow,l->getSelectedIndex(),by)) {
      int selected=l->getSelectedIndex()+by;showListSignature.clear();refreshShow();l->setSelectedIndex(selected);
    }
  });
  // (the admission's arrows, zoo.exe show_UpSpinner_click 0x4758aa /
  // show_DownSpinner_click 0x615576: a showAdmissionIncrement step, kept
  // to min/maxShowAdmission - shows.cfg's 0 and 12, read into 0x642c6c and
  // 0x642c68 at load, 0x5167ca; the moves, show1_MoveUpButton_click
  // 0x615662 / MoveDown 0x61569e: the picked trick a place up or down)
  for(auto [id,by]:{std::pair<int,int>{11105,1},{11106,-1}})button(id,[this,by]{
    if(shows)if(auto *s=shows->show(shownShow))shows->setAdmission(shownShow,s->admission+by*shows->settings.admissionStep);
  });
  if(auto *t=dynamic_cast<UiText*>(z->getElementById(11104))) {
    t->onBeginEdit=[this]{auto *s=shows?shows->show(shownShow):nullptr;return s?std::to_string(s->admission):"0";};
    // (zoo.exe show_AdmissionCost_click 0x61536b: the typed text's currency
    // symbol taken out once (text 22100, 0x5fe02d) and every thousands
    // separator (",", 0x5fe0a9), the rest read as a number, 0x4b40ae)
    t->onCommit=[this](const std::string &v){
      if(!shows)return;
      std::string text=v;
      const std::string sign=resource_manager->getString(22100);
      if(size_t at=sign.empty()?std::string::npos:text.find(sign);at!=std::string::npos)text.erase(at,sign.size());
      for(size_t at;(at=text.find(','))!=std::string::npos;)text.erase(at,1);
      shows->setAdmission(shownShow,std::atof(text.c_str()));
    };
  }
  z->syncTabs();
}

void UiGameScreen::showShow(int id) {
  if(!shows || !showPanelLayout)return;
  shows->refresh();if(!shows->show(id))return;
  shownShow=id;showListSignature.clear();
  showPanel(0,"");setPanelOpen(41,true);
  if(auto *b=dynamic_cast<UiButton*>(showPanelLayout->getElementById(11126)))b->choose();
  showPanelLayout->syncTabs();
  for(int sub:{44,45})if(auto *e=showPanelLayout->getElementById(sub))e->setHidden(true);
  // (the script's lists start with nothing picked, so Add, Remove and the
  // moves start off: zoo.exe ui_show1_468968 empties both lists, 0x2b67 and
  // 0x2b6b, before filling them; show1_Panel_show 0x4746a2 turns the four
  // buttons off and hides the frequency list)
  for(int id:{11111,11115})if(auto *l=dynamic_cast<UiListBox*>(showPanelLayout->getElementById(id)))l->setSelectedIndex(-1);
  refreshShow();
}

void UiGameScreen::pickShowFrequency(int row) {
  if(auto *l=showPanelLayout?dynamic_cast<UiListBox*>(showPanelLayout->getElementById(11191)):nullptr) {
    l->setSelectedIndex(row);showSelection(static_cast<UiAction>(71002));
  }
}

bool UiGameScreen::showSelection(UiAction a) {
  if(static_cast<int>(a)!=71001 && static_cast<int>(a)!=71002)return false;
  if(!shows || !showPanelLayout)return true;
  bool species=static_cast<int>(a)==71001;
  if(auto *l=dynamic_cast<UiListBox*>(showPanelLayout->getElementById(species?11196:11191))) {
    if(species)shows->selectSpecies(shownShow,l->getSelectedData());
    else shows->setFrequency(shownShow,showFrequencyOf(l->getSelectedIndex()));
  }
  if(auto *e=showPanelLayout->getElementById(species?45:44))e->setHidden(true);
  showListSignature.clear();refreshShow();return true;
}

void UiGameScreen::refreshShow() {
  if(!shows || !showPanelLayout || !isPanelOpen(41))return;
  shows->refresh();auto *s=shows->show(shownShow);
  if(!s){setPanelOpen(41,false);return;}
  auto *z=showPanelLayout;
  auto text=[z](int id,const std::string &v){if(auto *t=dynamic_cast<UiText*>(z->getElementById(id)))if(!t->isBeingEdited())t->setText(v);};
  auto button=[z](int id){return dynamic_cast<UiButton*>(z->getElementById(id));};
  auto choices=shows->species(shownShow);
  auto speciesName=[this](const std::string &file) {
    if(animals)for(const auto &t:animals->types())if(t.file==file)return t.displayName.empty()?resource_manager->getString(t.nameId):t.displayName;
    return file;
  };
  if(auto *ex=fences?fences->exhibit(shownShow):nullptr)text(11102,ex->name);
  text(11104,showMoney(std::lround(s->admission*100)));
  text(11120,std::to_string(shows->benefit(shownShow)));
  // (the species picked on its button, 11123; with none, nothing on it -
  // show.lyt gives it no text, and only a pick sets one: zoo.exe
  // showspec_list_click 0x6158a0, its name by the type's name id, +0x1c)
  if(auto *b=button(11123))b->setLabel(s->species.empty()?std::string():speciesName(s->species));
  // (the frequency's row picked and its words on the button, 11108:
  // zoo.exe ui_show1_468968, showtime_list_click 0x6156da - text 0x642c50[row])
  int frequencyIndex=frequencyRow(s->frequency);
  if(auto *l=dynamic_cast<UiListBox*>(z->getElementById(11191)))l->setSelectedIndex(frequencyIndex);
  if(auto *b=button(11108))b->setLabel(resource_manager->getString(11192+frequencyIndex));
  std::string status=s->status;
  if(s->phase==Shows::Phase::Gathering)status="Preparing show: gathering performers.";
  else if(s->phase==Shows::Phase::Performing)status="Show in progress.";
  else if(s->phase==Shows::Phase::Returning)status="Returning performers to their home tank.";
  else if(s->frequency<0)status="Shows disabled (Never).";
  if(auto *t=dynamic_cast<UiText*>(z->getElementById(71100)))
    if(t->getText()!=status)t->setMessage(status);
  bool active=s->phase!=Shows::Phase::Waiting;
  // A show set to Never can't be changed: Add, Remove, the moves, Select
  // Species, both trick lists, the admission and its arrows all off
  // (zoo.exe ui_show1_615794, the flag 0x64e808 set); else on again
  // (ui_show_4745a7), the arrows each off at its limit - up at
  // maxShowAdmission, down at minShowAdmission (ui_show_468865)
  const bool never=s->frequency<0;
  active=active || never;
  for(int id:{11113,11114,11117,11118,11123})if(auto *b=button(id))b->setDisabled(active);
  for(int id:{11111,11115,11104})if(auto *e=z->getElementById(id))e->setDisabled(never);
  if(auto *b=button(11105))b->setDisabled(never || s->admission>=shows->settings.admissionMax);
  if(auto *b=button(11106))b->setDisabled(never || s->admission<=shows->settings.admissionMin);
  std::string signature=s->species+"|";
  for(const auto &file:choices)signature+=file+";";
  for(const auto &key:s->script)signature+=key+",";
  for(const auto &t:shows->tricks(s->species))signature+=t.key+shows->unavailable(shownShow,s->species,t);
  if(signature!=showListSignature) {
    showListSignature=signature;
    if(auto *l=dynamic_cast<UiListBox*>(z->getElementById(11196))) {
      l->clear();for(const auto &file:choices)l->addItem(speciesName(file),file);
    }
    if(auto *l=dynamic_cast<UiListBox*>(z->getElementById(11111))) {
      std::string selected=l->getSelectedData();l->clear();int row=0;
      for(const auto &t:shows->tricks(s->species))if(t.visible) {
        std::string why=shows->unavailable(shownShow,s->species,t);
        l->addItem(t.name+(why.empty()?"":" (unavailable)"),t.key,t.icon);
        if(!why.empty())l->setItemColor(row,{144,144,144,255});
        if(t.key==selected)l->setSelectedIndex(row);++row;
      }
    }
    if(auto *l=dynamic_cast<UiListBox*>(z->getElementById(11115))) {
      int selected=l->getSelectedIndex();l->clear();
      for(const auto &key:s->script)for(const auto &t:shows->tricks(s->species))if(t.key==key)l->addItem(t.name,key,t.icon);
      l->setSelectedIndex(std::min(selected,static_cast<int>(s->script.size())-1));
    }
  }
  if(auto *l=dynamic_cast<UiListBox*>(z->getElementById(11111)))if(auto *b=button(11113)) {
    bool available=false;for(const auto &t:shows->tricks(s->species))if(t.key==l->getSelectedData())available=shows->unavailable(shownShow,s->species,t).empty();
    // (zoo.exe show1_AvailableTrickList_click 0x4743cf: on for an available
    // trick picked while no show runs - not off at the script's length)
    b->setDisabled(active || !available);
  }
  // (zoo.exe show1_TrickListInShow_click 0x474350: with a trick picked and
  // no show running, Remove on, Up on but for the first, Down on but for the
  // last; else all off)
  if(auto *l=dynamic_cast<UiListBox*>(z->getElementById(11115))) {
    int selected=l->getSelectedIndex();
    for(auto [id,disabled]:{std::pair<int,bool>{11114,selected<0},{11117,selected<=0},{11118,selected<0 || selected+1>=static_cast<int>(s->script.size())}})
      if(auto *b=button(id))b->setDisabled(active || disabled);
  }
  int score=shows->benefit(shownShow);
  if(auto *image=dynamic_cast<UiImage*>(z->getElementById(11121)))image->setImage(score>0?"ui/show/pos/pos":score<0?"ui/show/neg/neg":"ui/show/mid/mid");
  // zoo.exe 0x473b61 (Show Finances, show2.lyt): this month, last month,
  // the average a month (0x474e76, 0x474e4a, 0x474ea0: the total over the
  // months, 0x474d1f) and the total, for attendance (whole numbers,
  // 0x417879), income, upkeep (shown as costs, less than nothing) and profit
  // (income less upkeep) in money (formatMoney 0x4ef4d4, two decimals); the
  // months in operation under them (11173, 0x2ba5)
  const int months=shows->monthsOpen(*s);
  text(11140,std::to_string(s->visitorsNow));text(11141,std::to_string(s->visitorsLast));
  text(11142,std::to_string(s->visitors/months));text(11143,std::to_string(s->visitors));
  auto money=[&](int id,double v){text(id,v<0?"-"+showMoney(std::lround(-v*100)):showMoney(std::lround(v*100)));};
  money(11144,s->incomeNow);money(11145,s->incomeLast);money(11146,s->income/months);money(11147,s->income);
  money(11148,-s->upkeepNow);money(11149,-s->upkeepLast);money(11150,-(s->upkeepTotal/months));money(11151,-s->upkeepTotal);
  // A profit takes the colour of the upkeep's row when under nothing, of
  // the attendance's at nothing, of the income's above (the rows' own
  // forecolors, read by 0x473b61 from 11148, 11140 and 11144)
  auto colourOf=[z](int id){auto *t=dynamic_cast<UiText*>(z->getElementById(id));return t?t->layoutColor():SDL_Color{255,228,173,255};};
  const double perMonth=s->income/months-s->upkeepTotal/months;
  const std::pair<int,double> profits[]={{11152,s->incomeNow-s->upkeepNow},{11153,s->incomeLast-s->upkeepLast},
                                         {11154,perMonth},{11155,s->income-s->upkeepTotal}};
  for(const auto &[id,v]:profits) {
    money(id,v);
    if(auto *t=dynamic_cast<UiText*>(z->getElementById(id)))t->setTextColor(colourOf(v<0?11148:v==0?11140:11144));
  }
  text(11173,std::to_string(months));
}
