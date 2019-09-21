/* X-Cheng Wifi Module Source
 * Copyright (c) 2018-2028 Gene Kong
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */


#ifndef __WIFICONFIGMANAGER_H__
#define __WIFICONFIGMANAGER_H__

#include "osTask.h"

namespace FEmbed {

/**
 * @brief WifiConfigManager
 * We use this class to manage Wifi connections, such as APConfig or Smartconfig.
 */
class WifiConfigManager :
    public OSTask {
public:
    WifiConfigManager();
    virtual ~WifiConfigManager();

    virtual void loop();
    
    int connect();
    int disconnect();

    int startScan();
    int stopScan();

    int startSmartConfig();
    int stopSmartConfig();
};

}
#endif
