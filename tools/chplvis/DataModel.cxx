/*
 * Copyright 2015 Cray Inc.
 * Other additional copyright holders may be indicated within.
 *
 * The entirety of this work is licensed under the Apache License,
 * Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.
 *
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "DataModel.h"

// FLTK includes
#include <FL/fl_ask.H>

// C libraries

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <assert.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

// C++ Libraries
#include <set>

#ifndef MAXPATHLEN
#define MAXPATHLEN 2048
#endif

void DataModel::newList()
{
  //printf ("newList ...\n");
  curEvent = theEvents.begin();
  while (curEvent != theEvents.end()) {
    curEvent = theEvents.erase(curEvent);
  }
}

int DataModel::LoadData(const char * filename)
{
  const char *suffix;
  struct stat statbuf;
  char fullfilename[MAXPATHLEN];

  // Remove trailing / in name
  int fn_len = strlen(filename);
  char mfilename[fn_len+1];
  strcpy(mfilename, filename);
  if (mfilename[fn_len-1] == '/')
    mfilename[fn_len-1] = 0;

  if (stat(mfilename, &statbuf) < 0) {
    fl_message ("%s: %s.", filename, strerror(errno));
    return 0;
  }

  if ((statbuf.st_mode & S_IFMT) == S_IFDIR)
    snprintf (fullfilename, MAXPATHLEN, "%s/%s-0", mfilename, mfilename);
  else
    snprintf (fullfilename, MAXPATHLEN, "%s", filename);
  
  suffix = strrchr(fullfilename, '-');
  if (!suffix) {
    fl_message ("File %s does not appear to be generated by Chapel\n",
             fullfilename);
    return 0;
  }
  suffix += 1;
  int namesize = strlen(fullfilename) - strlen(suffix);
  // int fileno = atoi(suffix);

  // printf ("LoadData:  namesize is %d, fileno is %d\n", namesize, fileno);

  // printf ("loading data from %.*s* files ...", namesize, fullfilename);

  // fflush(stdout);

  newList();
  strDB.clear();
  curEvent = theEvents.begin();
  
  FILE *data = fopen(fullfilename, "r");
  if (!data) {
    fl_message ("LoadData: Could not open %s.\n", fullfilename);
    return 0;
  }

  // Read the config data
  char configline[100];

  if (fgets(configline, 100, data) != configline) {
    fl_message ("LoadData: Could not read file %s.\n", fullfilename);
    fclose(data);
    return 0;
  }

  // The configuration data
  int oldNumTags = numTags;
  int nlocales;
  int fnum;
  double seq;
  int VerMajor, VerMinor;

  int ssres = sscanf(configline, "ChplVdebug: ver %d.%d nodes %d nid %d tid %*d seq %lf",
                     &VerMajor, &VerMinor, &nlocales, &fnum, &seq);
  if (ssres  != 5) {
    fl_message ("\n  LoadData: incorrect data on first line of %s. (%d)\n",
             fullfilename, ssres);
    fclose(data);
    return 0;
  }
  fclose(data);

  // Should make this more parameterized !!!!
  if (VerMajor != 1 || VerMinor != 1) {
    fl_alert("Version mismatch in data files.\n");
  }

  char fname[namesize+15];
  // printf ("LoadData: nlocalse = %d, fnum = %d seq = %.3lf\n", nlocales, fnum, seq);

  // Set the number of locales.
  numLocales = nlocales;

  // Debug
  std::list<Event *>::iterator itr;
    
  for (int i = 0; i < nlocales; i++) {
    snprintf (fname, namesize+15, "%.*s%d", namesize, fullfilename, i);
    if (!LoadFile(fname, i, seq)) {
      fl_message ("Error processing data from %s\n", fname);
      numLocales = -1;
      return 0;
    }
    // Debug
    /* 

    printf ("\nAfter file %s\n", fname);
    itr = theEvents.begin();
    while (itr != theEvents.end()) {
      (*itr)->print();
      itr++;
    }
    printf ("---------------\n");
    */
    
  }
  
  // Build data structures, taglist: comms/tag

  if (tagList != NULL) {
    for (int i = 0 ; i < oldNumTags+2; i++ )
      delete tagList[i];
    delete [] tagList;
  }

  tagList = new tagData *[numTags+2];
  for (int i = 0 ; i < numTags+2; i++ ) {
    tagList[i] = new tagData(numLocales);
    // Special case ... Count one task for all locales[0].
    tagList[i]->maxTasks = tagList[i]->locales[0].numTasks = 1;
  }

  if (taskTimeline != NULL)
    delete [] taskTimeline;
  taskTimeline = new std::list<std::pair<Tl_Kind,long> >[numLocales];
  

  int cTagNo = TagStart;
  tagData *curTag = tagList[1];
      
  // printf ("\n\nList has %ld items\n", (long)theEvents.size());
  // printf ("List has %d tags\n", numTags);
  
  itr = theEvents.begin();
  while (itr != theEvents.end()) {

    // Data for processing events
    E_start  *sp = NULL;
    E_pause  *pp = NULL;
    E_tag    *gp = NULL;
    E_end    *ep = NULL;
    E_comm   *cp = NULL;
    E_fork   *fp = NULL;
    E_task   *tp = NULL;
    E_begin_task *btp = NULL;
    E_end_task   *etp = NULL;

    Event *ev = *itr;
    int curNodeId = ev->nodeId();
    curTag = tagList[cTagNo+2];

    //ev->print();                                          // Debug prints
    switch (ev->Ekind()) {

      case Ev_start:  // Update both -2 and -1 records (0 and 1)
        sp = (E_start *)ev;
        tagList[0]->locales[curNodeId].refUserCpu = 
            tagList[1]->locales[curNodeId].refUserCpu = sp->user_time();
        tagList[0]->locales[curNodeId].refSysCpu = 
            tagList[1]->locales[curNodeId].refSysCpu = sp->sys_time();
        tagList[0]->locales[curNodeId].refTime = 
            tagList[1]->locales[curNodeId].refTime = sp->clock_time();
        tagList[0]->name = "ALL";
        tagList[1]->name = "Start";
        tagList[0]->locales[0].maxConc = 1;
        tagList[1]->locales[0].runConc = 1;
        tagList[1]->locales[0].maxConc = 1;
        break;

      case Ev_tag:
        gp = (E_tag *)ev;
        if (curNodeId == 0) {
          cTagNo++;
          curTag = tagList[cTagNo+2];
          curTag->firstTag = itr;
          curTag->name = gp->tagName();
        }
        // Set ref times on current tag
        curTag->locales[curNodeId].refUserCpu = gp->user_time();
        curTag->locales[curNodeId].refSysCpu = gp->sys_time();
        curTag->locales[curNodeId].refTime = gp->clock_time();
        // Set ref times on -2 (All) if needed
        if (tagList[0]->locales[curNodeId].refTime == 0) {
          tagList[0]->locales[curNodeId].refUserCpu = gp->user_time();
          tagList[0]->locales[curNodeId].refSysCpu = gp->sys_time();
          tagList[0]->locales[curNodeId].refTime = gp->clock_time();
        }
        // Update times on previous tag
        curTag = tagList[cTagNo+1];
        if (curTag->locales[curNodeId].refTime != 0) {
          curTag->locales[curNodeId].userCpu += gp->user_time() -
            curTag->locales[curNodeId].refUserCpu;
          curTag->locales[curNodeId].sysCpu += gp->sys_time() -
            curTag->locales[curNodeId].refSysCpu;
          curTag->locales[curNodeId].Cpu = curTag->locales[curNodeId].userCpu +
             	                           curTag->locales[curNodeId].sysCpu;
          curTag->locales[curNodeId].clockTime += gp->clock_time() -
            curTag->locales[curNodeId].refTime;
          curTag->locales[curNodeId].refTime = 0;   // Reset for 
          // Update current tag maxes
          if (curTag->maxCpu < curTag->locales[curNodeId].Cpu) {
            curTag->maxCpu = curTag->locales[curNodeId].Cpu;
          }
          if (curTag->maxClock < curTag->locales[curNodeId].clockTime) {
            curTag->maxClock = curTag->locales[curNodeId].clockTime;
          }
          { // Remove the task record that started the "end record"
            std::map<long,taskData>::iterator it;
            it = curTag->locales[curNodeId].tasks.find(gp->vdbTid());
            if (it != curTag->locales[curNodeId].tasks.end()) {
                curTag->locales[curNodeId].tasks.erase((*it).first);
                // printf ("Removed task %d\n", gp->vdbTid());
            }
          }
        }
        break;

      case Ev_pause:    // Update information for current tag and All
        pp = (E_pause *)ev;
        for (int i = 0; i < 2; i++) {
          curTag->locales[curNodeId].userCpu += pp->user_time() -
            curTag->locales[curNodeId].refUserCpu;
          curTag->locales[curNodeId].sysCpu += pp->sys_time() -
            curTag->locales[curNodeId].refSysCpu;
          curTag->locales[curNodeId].Cpu = curTag->locales[curNodeId].userCpu +
            curTag->locales[curNodeId].sysCpu;
          curTag->locales[curNodeId].clockTime += pp->clock_time() -
            curTag->locales[curNodeId].refTime;
          curTag->locales[curNodeId].refTime = 0;   // Reset for 
          // Update current tag maxes
          if (curTag->maxCpu < curTag->locales[curNodeId].Cpu) {
            curTag->maxCpu = curTag->locales[curNodeId].Cpu;
          }
          if (curTag->maxClock < curTag->locales[curNodeId].clockTime) {
            curTag->maxClock = curTag->locales[curNodeId].clockTime;
          }
          // Remove last task to with Begin Rec but no End Rec
          { // Remove the task record that started the "end record"
            std::map<long,taskData>::iterator it;
            it = curTag->locales[curNodeId].tasks.find(pp->vdbTid());
            if (it != curTag->locales[curNodeId].tasks.end()) {
                curTag->locales[curNodeId].tasks.erase((*it).first);
                //printf ("Removed task %d\n", pp->vdbTid());
            }
          }
          // For 2nd time through loop, do the same thing for All
          curTag = tagList[0];
        }
        break;

      case Ev_end:   // Update both -2 and last tag
        ep = (E_end *)ev;
        for (int i = 0; i < 2; i++) {
          curTag->locales[curNodeId].userCpu += ep->user_time() -
            curTag->locales[curNodeId].refUserCpu;
          curTag->locales[curNodeId].sysCpu += ep->sys_time() -
            curTag->locales[curNodeId].refSysCpu;
          curTag->locales[curNodeId].Cpu = curTag->locales[curNodeId].userCpu +
            curTag->locales[curNodeId].sysCpu;
          curTag->locales[curNodeId].clockTime += ep->clock_time() -
            curTag->locales[curNodeId].refTime;
          curTag->locales[curNodeId].refTime = 0;   // Reset for 
          // Update current tag maxes
          if (curTag->maxCpu < curTag->locales[curNodeId].Cpu) {
            curTag->maxCpu = curTag->locales[curNodeId].Cpu;
          }
          if (curTag->maxClock < curTag->locales[curNodeId].clockTime) {
            curTag->maxClock = curTag->locales[curNodeId].clockTime;
          }
          { // Remove the task record that started the "end record"
            std::map<long,taskData>::iterator it;
            it = curTag->locales[curNodeId].tasks.find(ep->vdbTid());
            if (it != curTag->locales[curNodeId].tasks.end()) {
                curTag->locales[curNodeId].tasks.erase((*it).first);
                //printf ("Removed task %d\n", ep->vdbTid());
            }
          }
          // For 2nd time through loop, do the same thing for All
          curTag = tagList[0];
        }
        break;

      case Ev_comm:
        cp = (E_comm *)ev;
        for (int i = 0; i < 2; i++) {
          if (++(curTag->comms[cp->srcId()][cp->dstId()].numComms) > curTag->maxComms)
            curTag->maxComms = curTag->comms[cp->srcId()][cp->dstId()].numComms;
          curTag->comms[cp->srcId()][cp->dstId()].commSize += cp->totalLen();
          if (curTag->comms[cp->srcId()][cp->dstId()].commSize > curTag->maxSize)
            curTag->maxSize = curTag->comms[cp->srcId()][cp->dstId()].commSize;
          if (cp->isGet())
            curTag->comms[cp->srcId()][cp->dstId()].numGets++;
          else
            curTag->comms[cp->srcId()][cp->dstId()].numPuts++;
          // For 2nd time through loop, do the same thing for All
          curTag = tagList[0];
        }
        break;

      case Ev_fork:
        fp = (E_fork *)ev;
        for (int i = 0; i < 2; i++) {
          if (++(curTag->comms[fp->srcId()][fp->dstId()].numComms) > curTag->maxComms)
            curTag->maxComms = curTag->comms[fp->srcId()][fp->dstId()].numComms;
          curTag->comms[fp->srcId()][fp->dstId()].commSize += fp->argSize();
          if (curTag->comms[fp->srcId()][fp->dstId()].commSize > curTag->maxSize)
            curTag->maxSize = curTag->comms[fp->srcId()][fp->dstId()].commSize;
          curTag->comms[fp->srcId()][fp->dstId()].numForks++;
          // For 2nd time through loop, do the same thing for All
          curTag = tagList[0];
        }
        break;

      case Ev_task:
        tp = (E_task *)ev;
        // Insert tag into task map for this locale (No work for global)
        { 
          taskData newTask;
          newTask.taskRec = tp;
          std::pair<long,taskData> insPair(tp->taskId(), newTask);
          std::pair<std::map<long,taskData>::iterator,bool>
            rv = curTag->locales[curNodeId].tasks.insert(insPair);
          if (!rv.second) {
            fprintf (stderr, "Duplicate task! nodeId %d, taskId %ld\n",
                     curNodeId, tp->taskId());
          }
          //printf ("Created task %ld, tag %s node %d. %s\n", tp->taskId(),
          //        curTag->name.c_str(), curNodeId, tp->isLocal() ? "(local)": "");
        }
        break;

      case Ev_begin_task:
        btp = (E_begin_task *)ev;
        {
          std::map<long,taskData>::iterator it;
          // Find task in task map
          it = curTag->locales[curNodeId].tasks.find(btp->taskId());
          if (it != curTag->locales[curNodeId].tasks.end()) {
            // Update the begin record
            (*it).second.beginRec = btp;
            //printf ("Begin task %d, node %d\n", btp->taskId(), curNodeId);
          } else {
            printf ("(Begin task) No such task %ld in tag %s nodeid %d.\n",
                    btp->taskId(), curTag->name, curNodeId);
          }
        }
        break;

      case Ev_end_task:
        etp = (E_end_task *)ev;
        {
          std::map<long,taskData>::iterator it;
          // Find task in task map
          it = curTag->locales[curNodeId].tasks.find(etp->taskId());
          if (it != curTag->locales[curNodeId].tasks.end()) {
            double taskTime;
            // Update the end record
            it->second.endRec = etp;
            it->second.endTagNo = cTagNo;
            // Set task times
            taskTime = it->second.endRec->clock_time() - it->second.beginRec->clock_time();
            //printf ("Task %d/%ld taskTime %lf\n", curNodeId, etp->taskId(), taskTime);
            it->second.taskClock = taskTime;
            if (curTag->locales[curNodeId].maxTaskClock < taskTime)
              curTag->locales[curNodeId].maxTaskClock = taskTime;
            if (tagList[0]->locales[curNodeId].maxTaskClock < taskTime)
              tagList[0]->locales[curNodeId].maxTaskClock = taskTime;
            //printf ("End task %d, node %d\n", etp->taskId(), curNodeId);
          } else {
            bool validEnd = false;
            int tryTagNo = cTagNo-1;
            while (tryTagNo > DataModel::TagALL) {
              it = tagList[tryTagNo+2]->locales[curNodeId].tasks.find(etp->taskId());
              if (it != tagList[tryTagNo+2]->locales[curNodeId].tasks.end()) {
                it->second.endRec = etp;
                it->second.endTagNo = cTagNo;
                //printf ("Found end task %d in tag %d started in tag %s (%d), nid %d\n",
                //        etp->taskId(), cTagNo, tagList[tryTagNo+2]->name.c_str(),
                //        tryTagNo, curNodeId);
                validEnd = true;
                break;
              }
              tryTagNo--;
            }
            if (!validEnd) { // Erase this end record
              itr = theEvents.erase(itr);
              if (itr != theEvents.begin())
                itr--;
            }
          }
        }
        break;

      default:
        // Shouldn't get here
        assert(false);
    }
    // Move to next event record
    if (itr != theEvents.end())
      itr++;
  }

  // Go back and update task counts
  tagList[0]->locales[0].numTasks = 1;
  for (int ix_l = 1; ix_l < nlocales; ix_l++) {
    tagList[0]->locales[ix_l].numTasks = 0;
  }
  for (int ix_t = -1; ix_t < numTags; ix_t++) {
    curTag = tagList[ix_t+2];
    curTag->maxTasks = 0;
    // printf ("settings max for tag %s\n", getTagName(ix_t).c_str());
    for (int ix_l = 0; ix_l < nlocales; ix_l++) {
      // Total number of tasks
      //printf ("tag '%s', locale %d tasks %ld\n", curTag->name.c_str(), ix_l,
      //        (long)curTag->locales[ix_l].tasks.size());
      curTag->locales[ix_l].numTasks = curTag->locales[ix_l].tasks.size()
        + (ix_l == 0 ? 1 : 0 );
      if (curTag->locales[ix_l].numTasks > curTag->maxTasks)
        curTag->maxTasks = curTag->locales[ix_l].numTasks;
      // Calculate total tasks.   Don't count "main" over and over.
      tagList[0]->locales[ix_l].numTasks += curTag->locales[ix_l].numTasks - (ix_l == 0 ? 1 : 0);
      if (tagList[0]->locales[ix_l].numTasks > tagList[0]->maxTasks)
        tagList[0]->maxTasks = tagList[0]->locales[ix_l].numTasks;
    }
  }  

  // Build timeline and set concurrency rates
  itr = theEvents.begin();
  tagList[0]->maxConc = 1;
  cTagNo = TagStart;
  curTag = tagList[1];
  curTag->locales[0].maxConc = 1;
  curTag->maxConc = 1;
  
  while (itr != theEvents.end()) {
    Event *ev = *itr;
    int curNodeId = ev->nodeId();
    E_tag *tp;
    E_begin_task *btp;
    E_end_task *etp;
    E_comm   *cp;
    E_fork   *fp;
    
    switch (ev->Ekind()) {
      default: // Do nothing 
        break;

      case Ev_tag:
        tp = (E_tag *)ev;
        if (curNodeId == 0) {
          std::map<const char *, int>::iterator t_itr;
          cTagNo++;
          curTag = tagList[cTagNo+2];
          // Check for duplicate tags, map the tag name
          t_itr = name2tag.find(tp->tagName());
          if (t_itr != name2tag.end()) {
            // tag is duplicate
            uniqueTags = false;
          } else {
            // new tag, set a new index for it
            name2tag[tp->tagName()] = cTagNo;
          }
        }
        assert(cTagNo == tp->tagNo());
        taskTimeline[curNodeId].push_back(timelineEntry(Tl_Tag,cTagNo));
        curTag->locales[curNodeId].runConc
          = tagList[cTagNo+1]->locales[curNodeId].runConc;
        curTag->locales[curNodeId].maxConc = curTag->locales[curNodeId].runConc;
        if (curTag->locales[curNodeId].maxConc > curTag->maxConc)
          curTag->maxConc = curTag->locales[curNodeId].maxConc;
        //printf ("tag %d, %ld\n", curNodeId, curTag->locales[curNodeId].runConc);
        break;

      case Ev_begin_task:
        btp = (E_begin_task *)ev;
        if (curTag->locales[curNodeId].tasks.find(btp->taskId()) != 
            curTag->locales[curNodeId].tasks.end()) {
          // Found this task in the tag, it should be in the timeline
          taskTimeline[curNodeId].push_back(timelineEntry(Tl_Begin,btp->taskId()));
          curTag->locales[curNodeId].runConc++;
          //printf ("(++%d:%ld[%d])", curNodeId, curTag->locales[curNodeId].runConc,btp->taskId());
          if (curTag->locales[curNodeId].runConc > 
              curTag->locales[curNodeId].maxConc) {
            curTag->locales[curNodeId].maxConc = curTag->locales[curNodeId].runConc;
            if (curTag->locales[curNodeId].maxConc >
                tagList[0]->locales[curNodeId].maxConc)
                tagList[0]->locales[curNodeId].maxConc =
                  curTag->locales[curNodeId].maxConc;
            if (curTag->locales[curNodeId].maxConc > curTag->maxConc) {
              curTag->maxConc = curTag->locales[curNodeId].maxConc;
              if (curTag->maxConc > tagList[0]->maxConc)
                tagList[0]->maxConc = curTag->maxConc;
            }
          }
        }
        break;

      case Ev_end_task:
        etp = (E_end_task *)ev;
        {
          std::list< timelineEntry >::reverse_iterator tl_itr;
          tl_itr = taskTimeline[curNodeId].rbegin();
          while (tl_itr != taskTimeline[curNodeId].rend()) {
            if (tl_itr->first == Tl_Begin
                && tl_itr->second == etp->taskId()) {
              // Found the begin record in the timeline, add the end record
              taskTimeline[curNodeId].push_back(timelineEntry(Tl_End,etp->taskId()));
              curTag->locales[curNodeId].runConc--;
              //printf ("(--%d:%ld)", curNodeId, curTag->locales[curNodeId].runConc);
              break;
            }
            tl_itr++;
          }
          if (tl_itr == taskTimeline[curNodeId].rend())
            printf ("Timeline: did not find task %ld, nid %d\n", etp->taskId(), curNodeId);
        }
        break;

      case Ev_fork:
        fp = (E_fork *)ev;
        {
          taskData *theTask = getTaskData(curNodeId, fp->inTask());
          if (theTask != NULL) {
            // Insert the event
            theTask->commList.push_back(ev);
            theTask->commSum.numForks++;
            theTask->commSum.numComms++;
          } else 
            printf ("per task forks, no task %ld\n", (long)fp->inTask());
        }
        break;

      case Ev_comm:
        cp = (E_comm *)ev;
        {
          taskData *theTask = getTaskData(cp->isGet() ? cp->dstId() : cp->srcId(), cp->inTask());
          if (theTask != NULL) {
            // Insert the event
            theTask->commList.push_back(ev);
            theTask->commSum.numComms++;
            if (cp->isGet())
              theTask->commSum.numGets++;
            else
              theTask->commSum.numPuts++;
            theTask->commSum.commSize += cp->dataLen();
          } else 
            printf ("per task comms, no task %ld\n", (long)cp->inTask());
        }
        break;
    }

    // Move to next event record
    itr++;
  }

  // If duplicate tags, build unique tag information
  if (!uniqueTags) {
    int nextUtag;
    int tTag;

    //printf ("uniq tag no: %lu\n", name2tag.size());

    // Allocate tage data
    utagList = new tagData *[name2tag.size()];
    for (unsigned int ix = 0; ix < name2tag.size(); ix++)
      utagList[ix] = new tagData(numLocales);

    // All and Start use tagData ... see getUTagData
    // Now, aggrigate all duplicate tags, tag order is as first seen in tagList
    nextUtag = 0;   // Should be tag 2 in tagList
    for (int ix = 2; ix < numTags+2; ix++) {
      tTag = name2tag[tagList[ix]->name];
      //printf ("ix->tTag: %d -> %d\n", ix, tTag);
      if (tTag >= nextUtag) {
        name2tag[tagList[ix]->name] = nextUtag;
        tTag = nextUtag++;
        utagList[tTag]->name = tagList[ix]->name;
      }
      // merge data from tagList[ix] int utagList[tTag]
      for (int il = 0 ; il < numLocales; il ++ ) {
        // Ignore "ref" and "run" fields
        utagList[tTag]->locales[il].userCpu      += tagList[ix]->locales[il].userCpu;
        utagList[tTag]->locales[il].sysCpu       += tagList[ix]->locales[il].sysCpu;
        utagList[tTag]->locales[il].Cpu          += tagList[ix]->locales[il].Cpu;
        utagList[tTag]->locales[il].clockTime    += tagList[ix]->locales[il].clockTime;
        utagList[tTag]->locales[il].maxTaskClock += tagList[ix]->locales[il].maxTaskClock;
        utagList[tTag]->locales[il].numTasks     += tagList[ix]->locales[il].numTasks;
        if (utagList[tTag]->locales[il].maxConc  <  tagList[ix]->locales[il].maxConc)
          utagList[tTag]->locales[il].maxConc = tagList[ix]->locales[il].maxConc;
        // Update tag maxes
        if (utagList[tTag]->locales[il].Cpu > utagList[tTag]->maxCpu)
          utagList[tTag]->maxCpu = utagList[tTag]->locales[il].Cpu;
        if (utagList[tTag]->locales[il].clockTime > utagList[tTag]->maxClock)
          utagList[tTag]->maxClock = utagList[tTag]->locales[il].clockTime;
        if (utagList[tTag]->locales[il].numTasks > utagList[tTag]->maxTasks)
          utagList[tTag]->maxTasks = utagList[tTag]->locales[il].numTasks;
        if (utagList[tTag]->locales[il].maxConc > utagList[tTag]->maxConc)
          utagList[tTag]->maxConc = utagList[tTag]->locales[il].maxConc;
        // Communication
        for (int ic = 0; ic < numLocales; ic++ ) {
          utagList[tTag]->comms[il][ic].numComms += tagList[ix]->comms[il][ic].numComms;
          utagList[tTag]->comms[il][ic].numGets  += tagList[ix]->comms[il][ic].numGets;
          utagList[tTag]->comms[il][ic].numPuts  += tagList[ix]->comms[il][ic].numPuts;
          utagList[tTag]->comms[il][ic].numForks += tagList[ix]->comms[il][ic].numForks;
          utagList[tTag]->comms[il][ic].commSize += tagList[ix]->comms[il][ic].commSize;
          // Maxes
          if (utagList[tTag]->comms[il][ic].numComms > utagList[tTag]->maxComms)
            utagList[tTag]->maxComms = utagList[tTag]->comms[il][ic].numComms;
          if (utagList[tTag]->comms[il][ic].commSize > utagList[tTag]->maxSize)
            utagList[tTag]->maxSize = utagList[tTag]->comms[il][ic].commSize;
        }
      }
    }
  }

#if 0
  // Debug print of full DB
  printf ("Final Events DB.\n");
  itr = theEvents.begin();
  while (itr != theEvents.end()) {
    (*itr)->print();
    itr++;
  }

  // Timeline debug
  printf ("\nTimeline for node 0.\n");
  std::list<timelineEntry>::iterator tl_itr = taskTimeline[0].begin();
  while (tl_itr != taskTimeline[0].end()) {
    switch (tl_itr->first) {
      case Tl_Tag: printf ("Tag: %ld\n", tl_itr->second); break;
      case Tl_Begin: printf ("Begin: %ld\n", tl_itr->second); break;
      case Tl_End: printf ("End: %ld\n", tl_itr->second); break;
    }
    tl_itr++;
  }
#endif 

  return 1;
}


// Load the data in the current file

int DataModel::LoadFile (const char *filename, int index, double seq)
{
  FILE *data = fopen(filename, "r");
  char line[1024];

  int floc;        // Number of locales in the file
  int findex;      // current locale's index
  double fseq;
  int vdbTid;
  int VerMajor, VerMinor;

  int  nErrs = 0;

  if (!data) return 0;

  // printf ("LoadFile %s\n", filename);
  if (fgets(line,1024,data) != line) {
    fprintf (stderr, "Error reading file %s.\n", filename);
    return 0;
  }

  // Event times
  long e_sec, e_usec;

  // User/System time variables 
  long u_sec, u_usec, s_sec, s_usec;
  if (sscanf(line,
        "ChplVdebug: ver %d.%d nodes %d nid %d tid %d seq %lf %ld.%ld %ld.%ld %ld.%ld",
        &VerMajor, &VerMinor, &floc, &findex, &vdbTid, &fseq, &e_sec, &e_usec,
        &u_sec, &u_usec, &s_sec, &s_usec)
      != 12) {
    fprintf (stderr, "LoadData: incorrect data on first line of %s.\n",
             filename);
    fclose(data);
    return 0;
  }

  // Verify the data

  if (floc != numLocales || findex != index || fabs(seq-fseq) > .01 ) {
    //printf ("Mismatch %d = %d, %d = %d, %.3lf = %.3lf\n", floc, numLocales, findex,
    //        index, fseq, seq);
    fprintf (stderr, "Data file %s does not match selected file.\n", filename);
    return 0;
  }

  // Task Ids of tasks know to be part of the VisualDebug workings.  
  int nid0vdbtask = 0;
  std::set<int> vdbTids;  
  if (findex != 0)
    (void)vdbTids.insert(vdbTid);

  // Create a start event with starting user/sys times.
  std::list<Event *>::iterator itr = theEvents.begin();

  // Other initializations
  numTags = 0;

  // Now read the rest of the file
  Event *newEvent = new E_start(e_sec, e_usec, findex, u_sec, u_usec, s_sec, s_usec);
  if (itr == theEvents.end()) {
    theEvents.push_front(newEvent);
  } else {
    // Move past existing start events
    while ((*itr)->Ekind() == Ev_start) { itr++; }
    theEvents.insert(itr,newEvent);
  }

  while ( fgets(line, 1024, data) == line ) {
    // Common Data
    char *linedata;
    long sec;
    long usec;
    long nextCh;

    // Data for tasks and comm and fork
    int nid;    // Node id
    char onstr[5];  // "O" or "L" for onExecute or local
    int nlineno; // line number starting the task
    char nfilename[512];  // File name starting the task

    // comm
    int isGet;  // put (0), get (1)  currently ignoring non-block and strid
    int rnid;   // remote id
    long locAddr;  // local address
    long remAddr;  // remote address
    int eSize;     // element size
    int typeIx;    // type Index
    int dlen;      // data length 

    // fork
    int fid;

    // task
    int taskid;
    int parentId;

    // Tags
    int tagId;
    long nameOffset;  // Character offset for the tag name
    int slen;

    int cvt;

    // Process the line
    linedata = strchr(line, ':');
    if (linedata ) {
      if (sscanf (linedata, ": %ld.%ld%ln", &sec, &usec, &nextCh) != 2) {
        printf ("Can't read time from '%s'\n", linedata);
      }
    } else {
      nErrs++;
      continue;
    }

    newEvent = NULL;

    switch (line[0]) {

      case 0:  // Bug in output???
        nErrs++;
        break;

      case 'V': // VdbMark ... mark the taskID as being a vdbTask
        if (sscanf (&linedata[nextCh], "%d %d",
                    &nid, &taskid) != 2) {
          fprintf (stderr, "Bad VdbMark Line\n");
        } else {
          //printf ("VdbMark %d/%d\n", nid, taskid);
          if (nid == 0)
            nid0vdbtask = taskid;
          else
            (void)vdbTids.insert(taskid);
        }
        break;

      case 't':  // new task line
        //  task: s.u nodeID taskID O/L lineno filename
        if (sscanf (&linedata[nextCh], "%d %d %d %4s %d %511s",
                    &nid, &taskid, &parentId, onstr, &nlineno, nfilename) != 6) {
          fprintf (stderr, "Bad task line: %s\n", filename);
          fprintf (stderr, "nid = %d, taskid = %d, nbstr = '%s', nlineno = %d"
                   " nfilename = '%s'\n", nid, taskid, onstr, nlineno, nfilename);
          nErrs++;
        } else {
          if (vdbTids.find(parentId) != vdbTids.end() 
              || (nid == 0 && parentId == nid0vdbtask)) {
            // new task (taskid) is also a vdbtask
            // printf ("task is vdbtask: %d/%d\n", nid, taskid);
            (void)vdbTids.insert(taskid);
          } else {
            newEvent = new E_task (sec, usec, nid, taskid, onstr[0] == 'O',
                                   nlineno, strDB.getString(nfilename));
          }

        }
        break;

      case 'n':  // non-blocking put or get
      case 's':  // strid put or get
      case 'g':  // regular get
      case 'p':  // regular put
        // All comm data: 
        // s.u nodeID otherNode loc-addr rem-addr elemSize typeIndex len lineno filename
        if (sscanf (&linedata[nextCh], "%d %d %d 0x%lx 0x%lx %d %d %d %d %511s",
                    &nid, &rnid, &taskid, &locAddr, &remAddr, &eSize, & typeIx, &dlen,
                    &nlineno, nfilename) != 10) {
          fprintf (stderr, "Bad comm line: %s\n", filename);
          nErrs++;
        } else {
          if (vdbTids.find(taskid) != vdbTids.end()) {
            // Ignore this comm as being part of the xxxVdebug system
            break;
          }
          isGet = (line[0] == 'g' ? 1 :
                   line[0] == 'p' ? 0 :
                   line[3] == 'g' ? 1 : 0);
          if (isGet)
            newEvent = new E_comm (sec, usec, rnid, nid, eSize, dlen, isGet, taskid, nlineno,
                                   strDB.getString(nfilename));
          else
            newEvent = new E_comm (sec, usec, nid, rnid, eSize, dlen, isGet, taskid, nlineno,
                                   strDB.getString(nfilename));
        }
        break;

      case 'f':  // All the forks:
        // s.u nodeID otherNode subloc fid arg arg_size
        if ((cvt = sscanf (&linedata[nextCh], "%d %d %d %*d 0x%*x %d %d", 
                           &nid, &rnid, &fid, &dlen, &vdbTid)) != 5) {
          fprintf (stderr, "Bad fork line: (cvt %d) %s\n", cvt, filename);
          nErrs++;
        } else {
          if (vdbTids.find(vdbTid) != vdbTids.end()) {
            // printf ("fork vdbtid: %d/%d\n", nid, vdbTid);
            break;
          }
          newEvent = new E_fork(sec, usec, nid, rnid, dlen, line[1] == '_', vdbTid);
        }
        break;

      case 'P':  // Pause generating data
        if (sscanf (&linedata[nextCh], "%ld.%ld %ld.%ld %d %d %d",
                    &u_sec, &u_usec, &s_sec, &s_usec, &nid, &vdbTid, &tagId) != 7 ) {
          fprintf (stderr, "Bad 'End' line: %s\n", filename);
          nErrs++;
        } else {
          newEvent = new E_pause(sec, usec, nid, u_sec, u_usec,
                                 s_sec, s_usec, tagId, vdbTid);
          if (nid == 0)
            nid0vdbtask = 0;
        }
        break;

      case 'T':  // Tag in the data
        slen = strlen(line)-1;
        if (line[slen] == '\n') line[slen] = 0;
        // printf ("TagLine: '%s'\n", &linedata[nextCh]);
        if (sscanf (&linedata[nextCh], "%ld.%ld %ld.%ld %d %d %d %ln",
                    &u_sec, &u_usec, &s_sec, &s_usec, &nid, &vdbTid, &tagId, &nameOffset)
            != 7) {
          fprintf (stderr, "Bad 'Tag' line: %s\n", filename);
        } else {
          nextCh += nameOffset;
          newEvent = new E_tag(sec, usec, nid, u_sec, u_usec, s_sec, s_usec, tagId, 
                               strDB.getString(&linedata[nextCh]), vdbTid);
          if (tagId >= numTags)
            numTags = tagId+1;
          if (nid == 0) {
            nid0vdbtask = 0;
          }
        }
        break;

      case 'E':  // end of the file or End of task
        if (line[1] == 'n') {
          // End of file
          if (sscanf (&linedata[nextCh], "%ld.%ld %ld.%ld %d %d",
                      &u_sec, &u_usec, &s_sec, &s_usec, &nid, &vdbTid) != 6 ) {
            fprintf (stderr, "Bad 'End' line: %s\n", filename);
            nErrs++;
          } else {
            newEvent = new E_end(sec, usec, nid, u_sec, u_usec, s_sec, s_usec, vdbTid);
          }
        } else {
          // End of task
          // printf("E");
          if (sscanf (&linedata[nextCh], "%d %d",
                      &nid, &taskid ) != 2) {
            fprintf (stderr, "Bad Etask line: %s\n", filename);
            nErrs++;
          } else {
            if (vdbTids.find(taskid) == vdbTids.end()) {
              newEvent = new E_end_task(sec, usec, nid, taskid);
            } else {
              //printf ("end vdb tid: %d\n", taskid);
            }
          }
        }
        break;

      case 'B':  // Begin of task
        // printf ("B");
        if (sscanf (&linedata[nextCh], "%d %d",
                    &nid, &taskid ) != 2) {
          fprintf (stderr, "Bad Etask line: %s\n", filename);
          nErrs++;
        } else {
          if (vdbTids.find(taskid) == vdbTids.end()) {
            newEvent = new E_begin_task(sec, usec, nid, taskid);
          } else {
            //printf ("begin vdb tid: %d\n", taskid);
          }
        }
        break;
      
      default:
        /* Do nothing */ ;
        //printf ("d");
    }

    //  Add the newEvent to the list, group Starts, Tags, Resumes and Ends together.
    if (newEvent) {
      if (theEvents.empty()) {
        theEvents.push_front (newEvent);
      } else if (itr == theEvents.end()) {
        theEvents.insert(itr, newEvent);
      } else {
        if (newEvent->Ekind() <= Ev_end) {
          // Group together
          while (itr != theEvents.end()
                 && (*itr)->Ekind() != newEvent->Ekind())
            itr++;
          if (itr == theEvents.end() || (*itr)->Ekind() != newEvent->Ekind()) {
            fprintf (stderr, "Internal error, event mismatch. file '%s'\n", filename); \
            printf ("newEvent: "); newEvent->print();
            if (itr != theEvents.end()) {
               printf ("itr: "); (*itr)->print();
            } else {
               printf ("At end of list\n");
            }
          } else {
            // More complicated ... move past proper kinds ...
            E_tag *tp = NULL;
            if (newEvent->Ekind() == Ev_start || newEvent->Ekind() == Ev_end) {
              // Just find the end of the group
              while (itr != theEvents.end() && (*itr)->Ekind() == newEvent->Ekind())
                itr++;
            } else {
              // Need to move past them only if they have the same tag!
              if (newEvent->Ekind() == Ev_tag) {
                // Work with tags
                tp = (E_tag *)newEvent;
                while (itr != theEvents.end()
                       && (*itr)->Ekind() == Ev_tag
                       && ((E_tag *)(*itr))->tagNo() == tp->tagNo())
                  itr++;
              } else {
                // Work with pauses
                E_pause *rp = (E_pause *)newEvent;
                while (itr != theEvents.end()
                       && (*itr)->Ekind() == Ev_pause
                       && ((E_pause *)(*itr))->tagId() == rp->tagId())
                  itr++;
              }
            }
            /*std::list<Event*>::iterator newElem = */ theEvents.insert (itr, newEvent);
            //      if (tp != NULL && tp->nodeId() == 0) {
            //        tagVec[tp->tagNo()-1] = newElem;
            //      }
          }
        } else {
          // Insert by time
          while (itr != theEvents.end() &&
                 (*itr)->Ekind() > Ev_end &&
                 **itr < *newEvent)
            itr++;
          theEvents.insert (itr, newEvent);
        }
      }
    }
  }

  // Remove any task or Btask records that are in the vdbTids db.
  // printf ("Removing VDB events for node %d\n", findex);
  itr = theEvents.begin();
  while (itr != theEvents.end()) {
    bool doErase = false;
    Event *ev = *itr;
    // ev->print();
    if (ev->nodeId() == findex) {
      switch (ev->Ekind()) {
        case Ev_task:
          if (vdbTids.find(((E_task *)ev)->taskId()) != vdbTids.end()) {
            //printf ("deleting task %d/%ld\n", findex, ((E_task *)ev)->taskId());
            doErase = true;
          }
          break;
        case Ev_begin_task:
          if (vdbTids.find(((E_begin_task *)ev)->taskId()) != vdbTids.end()) {
            //printf ("deleting Btask %d/%ld\n", findex, ((E_begin_task *)ev)->taskId());
            doErase = true;
          }
          break;
        default:
          break;
      }
      if (doErase) 
        itr = theEvents.erase(itr);
      else
        itr++;
    } else {
      itr++;
    }
  }

  if (nErrs) fprintf(stderr, "%d errors in data file '%s'.\n", nErrs, filename);

  //  if (ignoreFork > 0 || ignoreTask > 0) {
  //    fprintf (stderr, "%s: Error in data filters: ignoreFork = %d, ignoreTask = %d\n",
  //         filename, ignoreFork, ignoreTask);
  //  }
  
  if ( !feof(data) ) return 0;
  
  return 1;
}

// Get the task data by task Id and locale.

taskData * DataModel::getTaskData (long locale, long taskId, long tagNo)
{
  std::map<long,taskData>::iterator tskItr;
  long curTag;

  if (locale == 0 && taskId == 1)
    return &mainTask;

  if (tagNo != TagALL) {
    tskItr = tagList[tagNo+2]->locales[locale].tasks.find(taskId);
    if (tskItr != tagList[tagNo+2]->locales[locale].tasks.end())
      return &(tskItr->second);
  }

  curTag = TagStart;
  while (curTag < numTags) {
    tskItr = tagList[curTag+2]->locales[locale].tasks.find(taskId);
    if (tskItr != tagList[curTag+2]->locales[locale].tasks.end())
      return &(tskItr->second);
    curTag++;
  }
  
  return NULL;
}
