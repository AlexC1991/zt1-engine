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
  if(auto *l=list(11191)) {
    l->clear();
    for(int id:{11195,11194,11193,11192})l->addItem(resource_manager->getString(id));
    l->setSelectionAction(static_cast<UiAction>(71002));
  }
  button(11113,[this,list]{if(shows)if(auto *l=list(11111)) {shows->add(shownShow,l->getSelectedData());showListSignature.clear();refreshShow();}});
  button(11114,[this,list]{if(shows)if(auto *l=list(11115)) {shows->remove(shownShow,l->getSelectedIndex());showListSignature.clear();refreshShow();}});
  for(auto [id,by]:{std::pair<int,int>{11117,-1},{11118,1}})button(id,[this,list,by]{
    if(shows)if(auto *l=list(11115))if(shows->move(shownShow,l->getSelectedIndex(),by)) {
      int selected=l->getSelectedIndex()+by;showListSignature.clear();refreshShow();l->setSelectedIndex(selected);
    }
  });
  for(auto [id,by]:{std::pair<int,int>{11105,1},{11106,-1}})button(id,[this,by]{
    if(shows)if(auto *s=shows->show(shownShow))shows->setAdmission(shownShow,s->admission+by*shows->settings.admissionStep);
  });
  if(auto *t=dynamic_cast<UiText*>(z->getElementById(11104))) {
    t->onBeginEdit=[this]{auto *s=shows?shows->show(shownShow):nullptr;return s?std::to_string(s->admission):"0";};
    t->onCommit=[this](const std::string &v){if(shows)shows->setAdmission(shownShow,std::atof(v.c_str()));};
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
  refreshShow();
}

bool UiGameScreen::showSelection(UiAction a) {
  if(static_cast<int>(a)!=71001 && static_cast<int>(a)!=71002)return false;
  if(!shows || !showPanelLayout)return true;
  bool species=static_cast<int>(a)==71001;
  if(auto *l=dynamic_cast<UiListBox*>(showPanelLayout->getElementById(species?11196:11191))) {
    if(species)shows->selectSpecies(shownShow,l->getSelectedData());
    else {int selected=l->getSelectedIndex();shows->setFrequency(shownShow,selected==3?-1:selected);}
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
  if(auto *b=button(11123))b->setLabel(s->species.empty()?"Select species":speciesName(s->species));
  int frequencyIndex=s->frequency==-1?3:s->frequency;
  if(auto *l=dynamic_cast<UiListBox*>(z->getElementById(11191)))l->setSelectedIndex(frequencyIndex);
  if(auto *b=button(11108))b->setLabel(resource_manager->getString(11195-frequencyIndex));
  std::string status=s->status;
  if(s->phase==Shows::Phase::Gathering)status="Preparing show: gathering performers.";
  else if(s->phase==Shows::Phase::Performing)status="Show in progress.";
  else if(s->phase==Shows::Phase::Returning)status="Returning performers to their home tank.";
  else if(s->frequency<0)status="Shows disabled (Never).";
  if(auto *t=dynamic_cast<UiText*>(z->getElementById(71100)))
    if(t->getText()!=status)t->setMessage(status);
  bool active=s->phase!=Shows::Phase::Waiting;
  for(int id:{11113,11114,11117,11118,11123})if(auto *b=button(id))b->setDisabled(active);
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
    b->setDisabled(active || !available || static_cast<int>(s->script.size())>=shows->settings.maxLength);
  }
  if(auto *l=dynamic_cast<UiListBox*>(z->getElementById(11115))) {
    int selected=l->getSelectedIndex();
    for(auto [id,disabled]:{std::pair<int,bool>{11114,selected<0},{11117,selected<=0},{11118,selected<0 || selected+1>=static_cast<int>(s->script.size())}})
      if(auto *b=button(id))b->setDisabled(active || disabled);
  }
  int score=shows->benefit(shownShow);
  if(auto *image=dynamic_cast<UiImage*>(z->getElementById(11121)))image->setImage(score>0?"ui/show/pos/pos":score<0?"ui/show/neg/neg":"ui/show/mid/mid");
  text(11143,std::to_string(s->visitors));text(11147,showMoney(std::lround(s->income*100)));
  // The total column is available now; monthly columns are maintained by the controller.
  for(int id:{11140,11141,11142,11144,11145,11146,11148,11149,11150,11151,11152,11153,11154,11155})text(id,"-");
}
