<?php

class acl extends plugin
{
  /* Definitions */
  var $plHeadline= "Access control";
  var $plDescription= "This does something";

  /* attribute list for save action */
  var $attributes= array('gosaAclEntry');
  var $objectclasses= array('gosaAcl');

  /* Helpers */
  var $dialogState= "head";
  var $gosaAclEntry= array();
  var $aclType= "";
  var $aclObject= "";
  var $aclContents= array();
  var $target= "group";
  var $aclTypes= array();
  var $aclObjects= array();
  var $aclMyObjects= array();
  var $users= array();
  var $groups= array();
  var $recipients= array();
  var $isContainer= FALSE;
  var $currentIndex= 0;
  var $wasNewEntry= FALSE;
  var $ocMapping= array();
  var $savedAclContents= array();


  function acl ($config, $parent, $dn= NULL)
  {
    /* Include config object */
    plugin::plugin($config, $dn);

    /* Load ACL's */
    $this->gosaAclEntry= array();
    if (isset($this->attrs['gosaAclEntry'])){
      for ($i= 0; $i<$this->attrs['gosaAclEntry']['count']; $i++){
        $acl= $this->attrs['gosaAclEntry'][$i];
        $this->gosaAclEntry= array_merge($this->gosaAclEntry, $this->explodeACL($acl));
      }
    }
    ksort($this->gosaAclEntry);

    /* Save parent - we've to know more about it than other plugins... */
    $this->parent= $parent;

    /* Container? */
    if (preg_match('/^(ou|c|l|dc)=/i', $dn)){
      $this->isContainer= TRUE;
    }

    /* Users */
    $ui= get_userinfo();
    $tag= $ui->gosaUnitTag;
    $ldap= $config->get_ldap_link();
    $ldap->cd($config->current['BASE']);
    if ($tag == ""){
      $ldap->search('(objectClass=gosaAccount)', array('uid', 'cn'));
    } else {
      $ldap->search('(&(objectClass=gosaAccount)(gosaUnitTag='.$tag.'))', array('uid', 'cn'));
    }
    while ($attrs= $ldap->fetch()){
      $this->users['U:'.$attrs['dn']]= $attrs['cn'][0].' ['.$attrs['uid'][0].']';
    }
    ksort($this->users);

    /* Groups */
    $ldap->cd($config->current['BASE']);
    if ($tag == ""){
      $ldap->search('(objectClass=posixGroup)', array('cn', 'description'));
    } else {
      $ldap->search('(&(objectClass=posixGroup)(gosaUnitTag='.$tag.'))', array('cn', 'description'));
    }
    while ($attrs= $ldap->fetch()){
      $dsc= "";
      if (isset($attrs['description'][0])){
        $dsc= $attrs['description'][0];
      }
      $this->groups['G:'.$attrs['dn']]= $attrs['cn'][0].' ['.$dsc.']';
    }
    ksort($this->groups);

    /* Objects */
    $tmp= get_global('plist');
    $plist= $tmp->info;
    if (isset($this->parent) && $this->parent != NULL){
      $oc= array();
      foreach ($this->parent->by_object as $key => $obj){
        $oc= array_merge($oc, $obj->objectclasses);
      }
      if (in_array_ics('organizationalUnit', $oc)){
        $this->isContainer= TRUE;
      }
    } else {
      $oc=  $this->attrs['objectClass'];
    }


    /* Extract available categories from plugin info list */
    foreach ($plist as $class => $acls){

      /* Only feed categories */
      if (isset($acls['plCategory'])){

        /* Walk through supplied list and feed only translated categories */
        foreach($acls['plCategory'] as $idx => $data){

          /* Non numeric index means -> base object containing more informations */
          if (preg_match('/^[0-9]+$/', $idx)){
            if (!isset($this->ocMapping[$data])){
              $this->ocMapping[$data]= array();
              $this->ocMapping[$data][]= '0';
            }
            $this->ocMapping[$data][]= $class;
          } else {
            if (!isset($this->ocMapping[$idx])){
              $this->ocMapping[$idx]= array();
              $this->ocMapping[$idx][]= '0';
            }
            $this->ocMapping[$idx][]= $class;
            $this->aclObjects[$idx]= $data['description'];

            /* Additionally filter the classes we're interested in in "self edit" mode */
            if (is_array($data['objectClass'])){
              foreach($data['objectClass'] as $objectClass){
                if (in_array_ics($objectClass, $oc)){
                  $this->myAclObjects[$idx.'/'.$class]= $acls['plDescription'];
                  break;
                }
              }
            } else {
              if (in_array_ics($data['objectClass'], $oc)){
                $this->myAclObjects[$idx.'/'.$class]= $acls['plDescription'];
              }
            }
          }

        }
      }
    }
    $this->aclObjects['all']= '*&nbsp;'._("All categories");
    $this->ocMapping['all']= array('0' => 'all');

    /* Sort categories */
    asort($this->aclObjects);

    /* Fill acl types */
    if ($this->isContainer){
      $this->aclTypes= array("reset" => _("Reset ACLs"),
                             "one" => _("One level"),
                             "base" => _("Current object"),
                             "sub" => _("Complete subtree"),
                             "psub" => _("Complete subtree (permanent)"));
                             //"role" => _("Use ACL defined in role"));
    } else {
      $this->aclTypes= array("base" => _("Current object"),
          "role" => _("Use ACL defined in role"));
    }
    asort($this->aclTypes);
    $this->targets= array("user" => _("Users"), "group" => _("Groups"));
    asort($this->targets);

    /* Finally - we want to get saved... */
    $this->is_account= TRUE;
  }


  function execute()
  {
    /* Call parent execute */
    plugin::execute();

    $tmp= get_global('plist');
    $plist= $tmp->info;

    /* Handle posts */
    if (isset($_POST['new_acl'])){
      $this->dialogState= 'create';
      $this->dialog= TRUE;
      $this->currentIndex= count($this->gosaAclEntry);
      $this->loadAclEntry(TRUE);
    }

    $new_acl= array();
    $aclDialog= FALSE;
    foreach($_POST as $name => $post){

      /* Actions... */
      if (preg_match('/^acl_edit_.*_x/', $name)){
        $this->dialogState= 'create';
        $this->dialog= TRUE;
        $this->currentIndex= preg_replace('/^acl_edit_([0-9]+).*$/', '\1', $name);
        $this->loadAclEntry();
        continue;
      }
      if (preg_match('/^acl_del_.*_x/', $name)){
        unset($this->gosaAclEntry[preg_replace('/^acl_del_([0-9]+).*$/', '\1', $name)]);
        continue;
      }

      if (preg_match('/^cat_edit_.*_x/', $name)){
        $this->aclObject= preg_replace('/^cat_edit_([^_]+)_.*$/', '\1', $name);
        $this->dialogState= 'edit';
        foreach ($this->ocMapping[$this->aclObject] as $oc){
          if (isset($this->aclContents[$oc])){
            $this->savedAclContents[$oc]= $this->aclContents[$oc];
          }
        }
        continue;
      }
      if (preg_match('/^cat_del_.*_x/', $name)){
        $idx= preg_replace('/^cat_del_([^_]+)_.*$/', '\1', $name);
        foreach ($this->ocMapping[$idx] as $key){
          unset($this->aclContents["$idx/$key"]);
        }
        continue;
      }

      /* Sorting... */
      if (preg_match('/^sortup_.*_x/', $name)){
        $index= preg_replace('/^sortup_([0-9]+).*$/', '\1', $name);
        if ($index > 0){
          $tmp= $this->gosaAclEntry[$index];
          $this->gosaAclEntry[$index]= $this->gosaAclEntry[$index-1];
          $this->gosaAclEntry[$index-1]= $tmp;
        }
        continue;
      }
      if (preg_match('/^sortdown_.*_x/', $name)){
        $index= preg_replace('/^sortdown_([0-9]+).*$/', '\1', $name);
        if ($index < count($this->gosaAclEntry)-1){
          $tmp= $this->gosaAclEntry[$index];
          $this->gosaAclEntry[$index]= $this->gosaAclEntry[$index+1];
          $this->gosaAclEntry[$index+1]= $tmp;
        }
        continue;
      }

      /* ACL saving... */
      if (preg_match('/^acl_.*_[^xy]$/', $name)){
        $aclDialog= TRUE;
        list($dummy, $object, $attribute, $value)= split('_', $name);

        /* Skip for detection entry */
        if ($object == 'dummy') {
          continue;
        }

        /* Ordinary ACLs */
        if (!isset($new_acl[$object])){
          $new_acl[$object]= array();
        }
        if (isset($new_acl[$object][$attribute])){
          $new_acl[$object][$attribute].= $value;
        } else {
          $new_acl[$object][$attribute]= $value;
        }
      }
    }
    
    /* Only be interested in new acl's, if we're in the right _POST place */
    if ($aclDialog && is_array($this->ocMapping[$this->aclObject])){

      foreach ($this->ocMapping[$this->aclObject] as $oc){
        unset($this->aclContents[$oc]);
        unset($this->aclContents[$this->aclObject.'/'.$oc]);
        if (isset($new_acl[$oc])){
          $this->aclContents[$oc]= $new_acl[$oc];
        }
        if (isset($new_acl[$this->aclObject.'/'.$oc])){
          $this->aclContents[$this->aclObject.'/'.$oc]= $new_acl[$this->aclObject.'/'.$oc];
        }
      }
    }

    /* Save new acl in case of base edit mode */
    if ($this->aclType == 'base'){
      $this->aclContents= $new_acl;
    }

    /* Cancel new acl? */
    if (isset($_POST['cancel_new_acl'])){
      $this->dialogState= 'head';
      $this->dialog= FALSE;
      if ($this->wasNewEntry){
        unset ($this->gosaAclEntry[$this->currentIndex]);
      }
    }

    /* Store ACL in main object? */
    if (isset($_POST['submit_new_acl'])){
      $this->gosaAclEntry[$this->currentIndex]['type']= $this->aclType;
      $this->gosaAclEntry[$this->currentIndex]['members']= $this->recipients;
      $this->gosaAclEntry[$this->currentIndex]['acl']= $this->aclContents;
      $this->dialogState= 'head';
      $this->dialog= FALSE;
    }

    /* Cancel edit acl? */
    if (isset($_POST['cancel_edit_acl'])){
      $this->dialogState= 'create';
      foreach ($this->ocMapping[$this->aclObject] as $oc){
        if (isset($this->savedAclContents[$oc])){
          $this->aclContents[$oc]= $this->savedAclContents[$oc];
        }
      }
    }

    /* Save edit acl? */
    if (isset($_POST['submit_edit_acl'])){
      $this->dialogState= 'create';
    }

    /* Add acl? */
    if (isset($_POST['add_acl']) && $_POST['aclObject'] != ""){
      $this->dialogState= 'edit';
      $this->savedAclContents= array();
      foreach ($this->ocMapping[$this->aclObject] as $oc){
        if (isset($this->aclContents[$oc])){
          $this->savedAclContents[$oc]= $this->aclContents[$oc];
        }
      }
    }

    /* Add to list? */
    if (isset($_POST['add']) && isset($_POST['source'])){
      foreach ($_POST['source'] as $key){
        if ($this->target == 'user'){
          $this->recipients[$key]= $this->users[$key];
        }
        if ($this->target == 'group'){
          $this->recipients[$key]= $this->groups[$key];
        }
      }
      ksort($this->recipients);
    }

    /* Remove from list? */
    if (isset($_POST['del']) && isset($_POST['recipient'])){
      foreach ($_POST['recipient'] as $key){
          unset($this->recipients[$key]);
      }
    }

    /* Save common values */
    foreach (array("aclType", "aclObject", "target") as $key){
      if (isset($_POST[$key])){
        $this->$key= validate($_POST[$key]);
      }
    }

    /* Create templating instance */
    $smarty= get_smarty();

    if ($this->dialogState == 'head'){
      /* Draw list */
      $aclList= new DivSelectBox("aclList");
      $aclList->SetHeight(450);
      
      /* Fill in entries */
      foreach ($this->gosaAclEntry as $key => $entry){
        $field1= array("string" => $this->aclTypes[$entry['type']], "attach" => "style='width:100px'");
        $field2= array("string" => $this->assembleAclSummary($entry));
        $action= "<input type='image' name='sortup_$key' alt='up' title='"._("Up")."' src='images/sort_up.png' align='top'>";
        $action.= "<input type='image' name='sortdown_$key' alt='down' title='"._("Down")."' src='images/sort_down.png'>";
        $action.= "<input class='center' type='image' src='images/edit.png' alt='"._("edit")."' name='acl_edit_$key' title='"._("Edit ACL")."'>";
        $action.= "<input class='center' type='image' src='images/edittrash.png' alt='"._("delete")."' name='acl_del_$key' title='"._("Delete ACL")."'>";

        $field3= array("string" => $action, "attach" => "style='border-right:0px;width:50px;text-align:right;'");
        $aclList->AddEntry(array($field1, $field2, $field3));
      }

      $smarty->assign("aclList", $aclList->DrawList());
    }

    if ($this->dialogState == 'create'){
      /* Draw list */
      $aclList= new DivSelectBox("aclList");
      $aclList->SetHeight(150);

      /* Add settings for all categories to the (permanent) list */
      foreach ($this->aclObjects as $section => $dsc){
        $summary= "";
        foreach($this->ocMapping[$section] as $oc){
          if (isset($this->aclContents[$oc]) && count($this->aclContents[$oc]) && isset($this->aclContents[$oc][0]) &&
              $this->aclContents[$oc][0] != ""){

            $summary.= "$oc, ";
            continue;
          }
          if (isset($this->aclContents["$section/$oc"]) && count($this->aclContents["$section/$oc"]) && isset($this->aclContents["$section/$oc"][0]) &&
              $this->aclContents["$section/$oc"][0] != ""){

            $summary.= "$oc, ";
            continue;
          }
          if (isset($this->aclContents[$oc]) && !isset($this->aclContents[$oc][0]) && count($this->aclContents[$oc])){
            $summary.= "$oc, ";
          }
        }

        /* Set summary... */
        if ($summary == ""){
          $summary= '<i>'._("No ACL settings for this category").'</i>';
        } else {
          $summary= sprintf(_("Contains ACLs for these objects: %s"), preg_replace('/, $/', '', $summary));
        }

        $field1= array("string" => $dsc, "attach" => "style='width:100px'");
        $field2= array("string" => $summary);
        $action= "<input class='center' type='image' src='images/edit.png' alt='"._("edit")."' name='cat_edit_$section' title='"._("Edit categories ACLs")."'>";
        $action.= "<input class='center' type='image' src='images/edittrash.png' alt='"._("delete")."' name='cat_del_$section' title='"._("Clear categories ACLs")."'>";
        $field3= array("string" => $action, "attach" => "style='border-right:0px;width:50px'");
        $aclList->AddEntry(array($field1, $field2, $field3));
      }

      $smarty->assign("aclList", $aclList->DrawList());
      $smarty->assign("aclType", $this->aclType);
      $smarty->assign("aclTypes", $this->aclTypes);
      $smarty->assign("target", $this->target);
      $smarty->assign("targets", $this->targets);

      /* Assign possible target types */
      $smarty->assign("targets", $this->targets);
      foreach ($this->attributes as $attr){
        $smarty->assign($attr, $this->$attr);
      }


      /* Generate list */
      $tmp= array();
      foreach (array("user" => "users", "group" => "groups") as $field => $arr){
        if ($this->target == $field){
          foreach ($this->$arr as $key => $value){
            if (!isset($this->recipients[$key])){
              $tmp[$key]= $value;
            }
          }
        }
      }
      $smarty->assign('sources', $tmp);
      $smarty->assign('recipients', $this->recipients);

      /* Acl selector if scope is base */
      if ($this->aclType == 'base'){
        $smarty->assign('aclSelector', $this->buildAclSelector($this->myAclObjects));
      }
    }

    if ($this->dialogState == 'edit'){
      $smarty->assign('headline', sprintf(_("Edit ACL for '%s', scope is '%s'"), $this->aclObjects[$this->aclObject], $this->aclTypes[$this->aclType]));

      /* Collect objects for selected category */
      foreach ($this->ocMapping[$this->aclObject] as $idx => $class){
        if ($idx == 0){
          continue;
        }
        $aclObjects[$this->aclObject.'/'.$class]= $plist[$class]['plDescription'];
      }
      if ($this->aclObject == 'all'){
        $aclObjects['all']= _("All objects in current subtree");
      }
      $smarty->assign('aclSelector', $this->buildAclSelector($aclObjects));
    }

    /* Show main page */
    $smarty->assign("dialogState", $this->dialogState);

    return ($smarty->fetch (get_template_path('acl.tpl')));
  }

  function sort_by_priority($list)
  {
    $tmp= get_global('plist');
    $plist= $tmp->info;
    asort($plist);

    foreach($list as $name => $translation){
      $na  =  preg_replace("/^.*\//","",$name);
      $prio=  $plist[$na]['plPriority'] ;

      $newSort[$name] = $prio;
    }

    asort($newSort);

    $ret = array();
    foreach($newSort as $name => $prio){
      $ret[$name] = $list[$name];
    }
    return($ret);
  }

  function buildAclSelector($list)
  {
    $display= "<input type='hidden' name='acl_dummy_0_0_0' value='1'>";
    $cols= 3;
    $tmp= get_global('plist');
    $plist= $tmp->info;
    asort($plist);

    /* Add select all/none buttons */
    $style = "style='width:100px;'";

    $display .= "<input ".$style." type='button' name='toggle_all_create' onClick=\"acl_toggle_all('_0_c$');\" value='Toggle C'>";
    $display .= "<input ".$style." type='button' name='toggle_all_move'   onClick=\"acl_toggle_all('_0_m$');\" value='Toggle M'>";
    $display .= "<input ".$style." type='button' name='toggle_all_remove' onClick=\"acl_toggle_all('_0_d$');\" value='Toggle D'> - ";
    $display .= "<input ".$style." type='button' name='toggle_all_read'   onClick=\"acl_toggle_all('_0_r$');\" value='Toggle R'>";
    $display .= "<input ".$style." type='button' name='toggle_all_write'  onClick=\"acl_toggle_all('_0_w$');\" value='Toggle W'> - ";
    
    $display .= "<input ".$style." type='button' name='toggle_all_sub_read'  onClick=\"acl_toggle_all('[^0]_r$');\" value='R+'>";
    $display .= "<input ".$style." type='button' name='toggle_all_sub_write'  onClick=\"acl_toggle_all('[^0]_w$');\" value='W+'>";
  
    $display .= "<br>";
  
    $style = "style='width:50px;'";
    $display .= "<input ".$style." type='button' name='set_true_all_create' onClick=\"acl_set_all('_0_c$',true);\" value='C+'>";
    $display .= "<input ".$style." type='button' name='set_false_all_create' onClick=\"acl_set_all('_0_c$',false);\" value='C-'>";
    $display .= "<input ".$style." type='button' name='set_true_all_move' onClick=\"acl_set_all('_0_m$',true);\" value='M+'>";
    $display .= "<input ".$style." type='button' name='set_false_all_move' onClick=\"acl_set_all('_0_m$',false);\" value='M-'>";
    $display .= "<input ".$style." type='button' name='set_true_all_remove' onClick=\"acl_set_all('_0_d$',true);\" value='D+'>";
    $display .= "<input ".$style." type='button' name='set_false_all_remove' onClick=\"acl_set_all('_0_d$',false);\" value='D-'> - ";
    $display .= "<input ".$style." type='button' name='set_true_all_read' onClick=\"acl_set_all('_0_r$',true);\" value='R+'>";
    $display .= "<input ".$style." type='button' name='set_false_all_read' onClick=\"acl_set_all('_0_r$',false);\" value='R-'>";
    $display .= "<input ".$style." type='button' name='set_true_all_write' onClick=\"acl_set_all('_0_w$',true);\" value='W+'>";
    $display .= "<input ".$style." type='button' name='set_false_all_write' onClick=\"acl_set_all('_0_w$',false);\" value='W-'> - ";

    $display .= "<input ".$style." type='button' name='set_true_all_read' onClick=\"acl_set_all('[^0]_r$',true);\" value='R+'>";
    $display .= "<input ".$style." type='button' name='set_false_all_read' onClick=\"acl_set_all('[^0]_r$',false);\" value='R-'>";
    $display .= "<input ".$style." type='button' name='set_true_all_write' onClick=\"acl_set_all('[^0]_w$',true);\" value='W+'>";
    $display .= "<input ".$style." type='button' name='set_false_all_write' onClick=\"acl_set_all('[^0]_w$',false);\" value='W-'>";
    /* Build general objects */

    $list =$this->sort_by_priority($list);
    foreach ($list as $key => $name){

      /* Create sub acl if it does not exist */
      if (!isset($this->aclContents[$key])){
        $this->aclContents[$key]= array();
        $this->aclContents[$key][0]= '';
      }
      $currentAcl= $this->aclContents[$key];

      /* Object header */
      if($_SESSION['js']) {
        if(isset($_SERVER['HTTP_USER_AGENT']) && preg_match("/gecko/i",$_SERVER['HTTP_USER_AGENT'])) {
          $display.= "\n<table style='width:100%;border:1px solid #A0A0A0' cellspacing=0 cellpadding=2>".
                     "\n  <tr>".
                     "\n    <td style='background-color:#C8C8C8;height:1.8em;' colspan=".($cols-1)."><b>"._("Object").": $name</b></td>".
                     "\n    <td align='right' style='background-color:#C8C8C8;height:1.8em;'>".
                     "\n    <input type='button' onclick='divtoggle(\"".preg_replace("/[^a-z0-9]/i","_",$name)."\");' value='"._("Show/Hide Advanced Settings")."' /></td>".
                     "\n  </tr>";
        } else if (isset($_SERVER['HTTP_USER_AGENT']) && preg_match("/ie/i",$_SERVER['HTTP_USER_AGENT'])) {
          $display.= "\n<table style='width:100%;border:1px solid #A0A0A0' cellspacing=0 cellpadding=2>".
                     "\n  <tr>".
                     "\n    <td style='background-color:#C8C8C8;height:1.8em;' colspan=".($cols-1)."><b>"._("Object").": $name</b></td>".
                     "\n    <td align='right' style='background-color:#C8C8C8;height:1.8em;'>".
                     "\n    <input type='button' onclick='divtoggle(\"".preg_replace("/[^a-z0-9]/i","_",$name)."\");' value='"._("Show/Hide Advanced Settings")."' /></td>".
                     "\n  </tr>";
        } else {
          $display.= "\n<table style='width:100%;border:1px solid #A0A0A0' cellspacing=0 cellpadding=2>".
                     "\n  <tr>".
                     "\n    <td style='background-color:#C8C8C8;height:1.8em;' colspan=$cols><b>"._("Object").": $name</b></td>".
                     "\n  </tr>";
        }
      } else {
          $display.= "\n<table style='width:100%;border:1px solid #A0A0A0' cellspacing=0 cellpadding=2>".
                     "\n  <tr>".
                     "\n    <td style='background-color:#C8C8C8;height:1.8em;' colspan=$cols><b>"._("Object").": $name</b></td>".
                     "\n  </tr>";
      }

      /* Generate options */
      $spc= "&nbsp;&nbsp;";
      if ($this->isContainer && $this->aclType != 'base'){
        $options= $this->mkchkbx($key."_0_c",  _("Create objects"), preg_match('/c/', $currentAcl[0])).$spc;
        $options.= $this->mkchkbx($key."_0_m", _("Move objects"), preg_match('/m/', $currentAcl[0])).$spc;
        $options.= $this->mkchkbx($key."_0_d", _("Remove objects"), preg_match('/d/', $currentAcl[0])).$spc;
        if ($plist[preg_replace('%^.*/%', '', $key)]['plSelfModify']){
          $options.= $this->mkchkbx($key."_0_s", _("Modifyable by owner"), preg_match('/s/', $currentAcl[0])).$spc;
        }
      } else {
        $options= $this->mkchkbx($key."_0_m", _("Move object"), preg_match('/m/', $currentAcl[0])).$spc;
        $options.= $this->mkchkbx($key."_0_d", _("Remove object"), preg_match('/d/', $currentAcl[0])).$spc;
        if ($plist[preg_replace('%^.*/%', '', $key)]['plSelfModify']){
          $options.= $this->mkchkbx($key."_0_s", _("Modifyable by owner"), preg_match('/s/', $currentAcl[0])).$spc;
        }
      }

      /* Global options */
      $more_options= $this->mkchkbx($key."_0_r",  _("read"), preg_match('/r/', $currentAcl[0])).$spc;
      $more_options.= $this->mkchkbx($key."_0_w", _("write"), preg_match('/w/', $currentAcl[0]));

      $display.= "\n  <tr>".
                 "\n    <td style='background-color:#E0E0E0' colspan=".($cols-1).">$options</td>".
                 "\n    <td style='background-color:#D4D4D4'>&nbsp;".("Complete object:")." $more_options</td>".
                 "\n  </tr>";

      /* Walk through the list of attributes */
      $cnt= 1;
      $splist= $plist[preg_replace('%^.*/%', '', $key)]['plProvidedAcls'];
      asort($splist);
      if($_SESSION['js']) {
        if(isset($_SERVER['HTTP_USER_AGENT']) && preg_match("/gecko/i",$_SERVER['HTTP_USER_AGENT'])) {
          $display.= "\n  <tr id='tr_".preg_replace("/[^a-z0-9]/i","_",$name)."' style='vertical-align:top;height:0px;'>".
                     "\n    <td colspan=".$cols.">".
                     "\n      <div id='".preg_replace("/[^a-z0-9]/i","_",$name)."' style='overflow:hidden;visibility:hidden;height:0px;vertical-align:top;width:100%;'>".
                     "\n        <table style='width:100%;'>";
        } else if (isset($_SERVER['HTTP_USER_AGENT']) && preg_match("/ie/i",$_SERVER['HTTP_USER_AGENT'])) {
          $display.= "\n  <tr id='tr_".preg_replace("/[^a-z0-9]/i","_",$name)."' style='vertical-align:top;height:0px;'>".
                     "\n    <td colspan=".$cols.">".
                     "\n      <div id='".preg_replace("/[^a-z0-9]/i","_",$name)."' style='position:absolute;overflow:hidden;visibility:hidden;height:0px;vertical-align:top;width:100%;'>".
                     "\n        <table style='width:100%;'>";
        }
      }
      foreach($splist as $attr => $dsc){

        /* Skip pl* attributes, they are internal... */
        if (preg_match('/^pl[A-Z]+.*$/', $attr)){
          continue;
        }

        /* Open table row */
        if ($cnt == 1){
          $display.= "\n  <tr>";
        }

        /* Close table row */
        if ($cnt == $cols){
          $cnt= 1;
          $rb= "";
          $end= "\n  </tr>";
        } else {
          $cnt++;
          $rb= "border-right:1px solid #A0A0A0;";
          $end= "";
        }

        /* Collect list of attributes */
        $state= "";
        if (isset($currentAcl[$attr])){
          $state= $currentAcl[$attr];
        }
        $display.= "\n    <td style='border-top:1px solid #A0A0A0;${rb}width:".(int)(100/$cols)."%'>".
                   "\n      <b>$dsc</b> ($attr)<br>".$this->mkrwbx($key."_".$attr, $state)."</td>$end";
      }
      
      /* Fill missing td's if needed */
      if (--$cnt != $cols && $cnt != 0){
       $display.= str_repeat("\n    <td style='border-top:1px solid #A0A0A0; width:".(int)(100/$cols)."%'>&nbsp;</td>", $cols-$cnt); 
      }

      if($_SESSION['js']) {
        if(isset($_SERVER['HTTP_USER_AGENT']) && (preg_match("/gecko/i",$_SERVER['HTTP_USER_AGENT'])) || (preg_match("/ie/i",$_SERVER['HTTP_USER_AGENT']))) {
          $display.= "\n        </table>".
                     "\n      </div>".
                     "\n    </td>".
                     "\n  </tr>";
        }
      }

      $display.= "\n</table><br />\n";
    }

    return ($display);
  }


  function mkchkbx($name, $text, $state= FALSE)
  {
    $state= $state?"checked":"";
    return "\n      <input id='acl_".preg_replace("/[^a-z0-9]/i","_",$name)."' type=checkbox name='acl_$name' $state>".
           "\n      <label for='acl_$name'>$text</label>";
  }


  function mkrwbx($name, $state= "")
  {
    $rstate= preg_match('/r/', $state)?'checked':'';
    $wstate= preg_match('/w/', $state)?'checked':'';
    return ("\n      <input id='acl_".preg_replace("/[^a-z0-9]/i","_",$name)."_r' type=checkbox name='acl_${name}_r' $rstate>".
            "\n      <label for='acl_${name}_r'>"._("read")."</label>".
            "\n      <input id='acl_".preg_replace("/[^a-z0-9]/i","_",$name)."_w' type=checkbox name='acl_${name}_w' $wstate>".
            "\n      <label for='acl_${name}_w'>"._("write")."</label>");
  }


  function explodeACL($acl)
  {
    list($index, $type)= split(':', $acl);
    $a= array( $index => array("type" => $type,
                               "members" => acl::extractMembers($acl)));
    
    /* Handle different types */
    switch ($type){

      case 'psub':
      case 'sub':
      case 'one':
      case 'base':
        $a[$index]['acl']= acl::extractACL($acl);
        break;
      
      case 'role':
        echo "Role";
        break;

      case 'reset':
        break;
      
      default:
        print_red(sprintf(_("Unkown ACL type '%s'. Don't know how to handle it."), $type));
        $a= array();
    }
    
    return ($a);
  }


  function extractMembers($acl)
  {
    global $config;
    $a= array();

    /* Rip acl off the string, seperate by ',' and place it in an array */
    $ms= preg_replace('/^[^:]+:[^:]+:([^:]+).*$/', '\1', $acl);
    if ($ms == $acl){
      return $a;
    }
    $ma= split(',', $ms);

    /* Decode dn's, fill with informations from LDAP */
    $ldap= $config->get_ldap_link();
    foreach ($ma as $memberdn){
      $dn= base64_decode($memberdn);
      $ldap->cat($dn, array('cn', 'objectClass', 'description', 'uid'));

      /* Found entry... */
      if ($ldap->count()){
        $attrs= $ldap->fetch();
        if (in_array_ics('gosaAccount', $attrs['objectClass'])){
          $a['U:'.$dn]= $attrs['cn'][0]." [".$attrs['uid'][0]."]";
        } else {
          $a['G:'.$dn]= $attrs['cn'][0];
          if (isset($attrs['description'][0])){
            $a['G:'.$dn].= " [".$attrs['description'][0]."]";
          }
        }

      /* ... or not */
      } else {
        $a['U:'.$dn]= sprintf(_("Unknown entry '%s'!"), $dn);
      }
    }

    return ($a);
  }


  function extractACL($acl)
  {
    /* Rip acl off the string, seperate by ',' and place it in an array */
    $as= preg_replace('/^[^:]+:[^:]+:[^:]*:(.*)$/', '\1', $acl);
    $aa= split(',', $as);
    $a= array();

    /* Dis-assemble single ACLs */
    foreach($aa as $sacl){
      
      /* Dis-assemble field ACLs */
      $ao= split('#', $sacl);
      $gobject= "";
      foreach($ao as $idx => $ssacl){

        /* First is department with global acl */
        $object= preg_replace('/^([^;]+);.*$/', '\1', $ssacl);
        $gacl=   preg_replace('/^[^;]+;(.*)$/', '\1', $ssacl);
        if ($idx == 0){
          /* Create hash for this object */
          $gobject= $object;
          $a[$gobject]= array();

          /* Append ACL if set */
          if ($gacl != ""){
            $a[$gobject]= array($gacl);
          }
        } else {

          /* All other entries get appended... */
          list($field, $facl)= split(';', $ssacl);
          $a[$gobject][$field]= $facl;
        }

      }
    }

    return ($a);
  }

  
  function assembleAclSummary($entry)
  {
    $summary= "";

    /* Summarize ACL */
    if (isset($entry['acl'])){
      $acl= "";
      foreach ($entry['acl'] as $name => $object){
        if (count($object)){
          $acl.= "$name, ";
        }
      }
      $summary.= sprintf(_("Contains settings for these objects: %s"), preg_replace('/, $/', '', $acl));
    }

    /* Summarize members */
    if ($summary != ""){
      $summary.= ", ";
    }
    if (count($entry['members'])){
      $summary.= _("Members:")." ";
      foreach ($entry['members'] as $cn){
        $cn= preg_replace('/ \[.*$/', '', $cn);
        $summary.= $cn.", ";
      }
    } else {
      $summary.= _("ACL is valid for all users");
    }

    return (preg_replace('/, $/', '', $summary));
  }


  function loadAclEntry($new= FALSE)
  {
    /* New entry gets presets... */
    if ($new){
      $this->aclType= 'base';
      $this->recipients= array();
      $this->aclContents= array();
    } else {
      $acl= $this->gosaAclEntry[$this->currentIndex];
      $this->aclType= $acl['type'];
      $this->recipients= $acl['members'];
      $this->aclContents= $acl['acl'];
    }

    $this->wasNewEntry= $new;
  }


  function aclPostHandler()
  {
    if (isset($_POST['save_acl'])){
      $this->save();
      return TRUE;
    }

    return FALSE;
  }


  function save()
  {
    /* Assemble ACL's */
    $tmp_acl= array();
    foreach ($this->gosaAclEntry as $prio => $entry){
      $final= "";
      $members= "";
      if (isset($entry['members'])){
        foreach ($entry['members'] as $key => $dummy){
          $members.= base64_encode(preg_replace('/^.:/', '', $key)).',';
        }
      }
      $final= $prio.":".$entry['type'].":".preg_replace('/,$/', '', $members);

      /* ACL's if needed */
      if ($entry['type'] != "reset" && $entry['type'] != "role"){
        $acl= ":";
        if (isset($entry['acl'])){
          foreach ($entry['acl'] as $object => $contents){

            /* Only save, if we've some contents in there... */
            if (count($contents)){
              $acl.= $object.";";

              foreach($contents as $attr => $permission){

                /* First entry? Its the one for global settings... */
                if ($attr == '0'){
                  $acl.= $permission;
                } else {
                  $acl.= '#'.$attr.';'.$permission;
                }

              }
              $acl.= ',';
            }
            
          }
        }
        $final.= preg_replace('/,$/', '', $acl);
      }

      $tmp_acl[]= $final;
    } 

    /* Call main method */
    plugin::save();

    /* Finally (re-)assign it... */
    $this->attrs['gosaAclEntry']= $tmp_acl;

    /* Remove acl from this entry if it is empty... */
    if (!count($tmp_acl)){
      /* Remove attribute */
      if ($this->initially_was_account){
        $this->attrs['gosaAclEntry']= array();
      } else {
        if (isset($this->attrs['gosaAclEntry'])){
          unset($this->attrs['gosaAclEntry']);
        }
      }

      /* Remove object class */
      $this->attrs['objectClass']= array_remove_entries(array('gosaAcl'), $this->attrs['objectClass']);
    }    

    /* Do LDAP modifications */
    $ldap= $this->config->get_ldap_link();
    $ldap->cd($this->dn);
    $this->cleanup();
    $ldap->modify ($this->attrs);

    show_ldap_error($ldap->get_error(), sprintf(_("Saving ACLs with dn '%s' failed."),$this->dn));

    /* Refresh users ACLs */
    $ui= get_userinfo();
    $ui->loadACL();
    $_SESSION['ui']= $ui;
  }


  function remove_from_parent()
  {
  }

}

// vim:tabstop=2:expandtab:shiftwidth=2:filetype=php:syntax:ruler:
?>