##########################################################################
#  
#  Copyright (c) 2011, John Haddon. All rights reserved.
#  Copyright (c) 2011-2012, Image Engine Design Inc. All rights reserved.
#  
#  Redistribution and use in source and binary forms, with or without
#  modification, are permitted provided that the following conditions are
#  met:
#  
#      * Redistributions of source code must retain the above
#        copyright notice, this list of conditions and the following
#        disclaimer.
#  
#      * Redistributions in binary form must reproduce the above
#        copyright notice, this list of conditions and the following
#        disclaimer in the documentation and/or other materials provided with
#        the distribution.
#  
#      * Neither the name of John Haddon nor the names of
#        any other contributors to this software may be used to endorse or
#        promote products derived from this software without specific prior
#        written permission.
#  
#  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
#  IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
#  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
#  PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
#  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
#  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
#  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
#  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
#  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
#  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
#  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#  
##########################################################################

import Gaffer
import GafferUI

QtCore = GafferUI._qtImport( "QtCore" )

class Dialogue( GafferUI.Window ) :

	## \todo Remove the deprecated resizeable argument
	def __init__( self, title, borderWidth=8, resizeable=None, sizeMode=GafferUI.Window.SizeMode.Manual, **kw ) :
	
		GafferUI.Window.__init__( self, title, borderWidth, resizeable, sizeMode=sizeMode, **kw )
		
		self._qtWidget().setWindowFlags( QtCore.Qt.WindowFlags( QtCore.Qt.Dialog ) )
		
		self.__column = GafferUI.ListContainer( GafferUI.ListContainer.Orientation.Vertical, spacing = 8 )
		
		self.__column.append( GafferUI.Frame(), True )
		
		self.__buttonRow = GafferUI.ListContainer( GafferUI.ListContainer.Orientation.Horizontal, spacing=8 )
		self.__column.append( self.__buttonRow )
		
		self.setChild( self.__column )
	
	## Enters a modal state	and returns the button the user pressed to exit
	# that state, or None if the dialogue was closed instead. If parentWindow
	# is specified then the dialogue will be temporarily parented on top of the
	# window for the duration of the call. Note that if parentWindow is being
	# shown fullscreen, it is critical to use the parentWindow argument to avoid
	# the dialogue disappearing in strange ways.
	def waitForButton( self, parentWindow=None ) :
	
		assert( len( self.__buttonRow ) )
	
		if parentWindow is not None :
			focusWidget = self._qtWidget().focusWidget()
			parentWindow.addChildWindow( self )
			if focusWidget is not None :
				# the reparenting above removes the focus, so we reclaim it.
				# this is important for PathChooserWidget, which puts the focus
				# in the right place in waitForPath(), then calls waitForButton().
				focusWidget.setFocus( QtCore.Qt.ActiveWindowFocusReason )
			
		self.setVisible( False )
		self._qtWidget().setWindowModality( QtCore.Qt.ApplicationModal )
		self.setVisible( True )
		
		self.__eventLoop = GafferUI.EventLoop()		
		self.__buttonConnections = []
		self.__closeConnection = self.closedSignal().connect( Gaffer.WeakMethod( self.__close ) )
		for button in self.__buttonRow :
			self.__buttonConnections.append( button.clickedSignal().connect( Gaffer.WeakMethod( self.__buttonClicked ) ) )
		
		self.__resultOfWait = None
		self.__eventLoop.start() # returns when a button has been pressed
		self.__buttonConnections = []
		self.__closeConnection = None
		self.__eventLoop = None
		
		self._qtWidget().setWindowModality( QtCore.Qt.NonModal )
		
		if parentWindow is not None :
			parentWindow.removeChild( self )
							
		return self.__resultOfWait
		
	def _setWidget( self, widget ) :
	
		self.__column[0] = widget
		
	def _getWidget( self ) :
		
		return self.__column[0]
		
	def _addButton( self, textOrButton ) :
		
		assert( isinstance( textOrButton, ( basestring, GafferUI.Button ) ) )
		
		if isinstance( textOrButton, basestring ) :
			button = GafferUI.Button( textOrButton )
		else :
			button = textOrButton
		
		self.__buttonRow.append( button )		

		return button
		
	def __buttonClicked( self, button ) :
	
		# check we're in a call to _waitForButton
		assert( len( self.__buttonConnections ) and self.__eventLoop is not None )
		
		self.__resultOfWait = button
		if self.__eventLoop.running() : # may already have stopped in __close (if a subclass called close())
			self.__eventLoop.stop()
		
	def __close( self, widget ) :
	
		# check we're in a call to _waitForButton
		assert( len( self.__buttonConnections ) and self.__eventLoop is not None  )

		self.__resultOfWait = None
		if self.__eventLoop.running() : # may already have stopped in __buttonClicked (if same button later triggered close())
			self.__eventLoop.stop()