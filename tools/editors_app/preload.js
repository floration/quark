// Quarkology Editors: the tab bar's two buttons (open the saves folder, show where it is)
const { contextBridge, ipcRenderer } = require('electron');
contextBridge.exposeInMainWorld('editors', {
  tabs: () => ipcRenderer.invoke('tabs'),
  openSaves: () => ipcRenderer.invoke('open-saves'),
});
