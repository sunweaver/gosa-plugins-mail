<?php
/*
   This code is part of GOsa (https://gosa.gonicus.de)
   Copyright (C) 2004  Cajus Pollmeier

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

class mailMethodSendmailCyrus extends mailMethodCyrus
{

  function mailMethodSendmailCyrus(&$config)
  {
    /* Check if the mail account identification attribute
       is overridden in the configuration file
     */
    if(isset($config->current['MAIL_ATTRIB']) && !empty($config->current['MAIL_ATTRIB'])){
      $new_uattrib= strtolower($config->current['MAIL_ATTRIB']);
      if(in_array($new_uattrib,array("mail","uid"))){
        $this->uattrib = $new_uattrib;
      }else{
        trigger_error(sprintf("Unsupported MAIL_ATTRIB in gosa configuration specified, use 'mail' or 'uid', instead of '%s'.",            $new_uattrib));
      }
    }

    $this->config= $config->data['SERVERS']['IMAP'];
  }

  function fixAttributesOnLoad(&$mailObject)
  {
    /* Remove possible local alias from forwarders */
    $newForwarder= array();
    foreach ($mailObject->gosaMailForwardingAddress as $addr){
      if (!preg_match('/^\\\\/', $addr)){
        $newForwarder[]= $addr;
      }
    }
    $mailObject->gosaMailForwardingAddress= $newForwarder;
  }

  function fixAttributesOnStore(&$mailObject)
  {
    /* Add local user if checked */
    if (preg_match("/L/", $mailObject->gosaMailDeliveryMode)) {
      $mailObject->gosaMailForwardingAddress[]= "\\".$mailObject->uid;
    }
  }

}

// vim:tabstop=2:expandtab:shiftwidth=2:filetype=php:syntax:ruler:
?>